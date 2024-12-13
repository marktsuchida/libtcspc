/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/acquire.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/introspect.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"
#include "test_thread_utils.hpp"

// Trompeloeil requires catch2 to be included first.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/trompeloeil.hpp>
#include <trompeloeil/mock.hpp>
#include <trompeloeil/sequence.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

namespace tcspc {

TEST_CASE("type constraints: acquire") {
    auto ctx = context::create();
    using proc_type = decltype(acquire<int>(
        null_reader<int>(), new_delete_bucket_source<int>::create(),
        arg::batch_size<>{64}, ctx->tracker<acquire_access>("acq"),
        sink_events<bucket<int>>()));
    STATIC_CHECK(is_processor_v<proc_type>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("type constraints: acquire_full_buckets") {
    auto ctx = context::create();
    using proc_type = decltype(acquire_full_buckets<int>(
        null_reader<int>(), sharable_new_delete_bucket_source<int>::create(),
        arg::batch_size<>{64}, ctx->tracker<acquire_access>("acq"),
        sink_events<bucket<int const>>(), sink_events<bucket<int>>()));
    STATIC_CHECK(is_processor_v<proc_type>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("introspect: acquire") {
    auto ctx = context::create();
    check_introspect_simple_processor(acquire<int>(
        null_reader<int>(), new_delete_bucket_source<int>::create(),
        arg::batch_size<>{64}, ctx->tracker<acquire_access>("acq"),
        null_sink()));
}

TEST_CASE("introspect: acquire_full_buckets") {
    auto ctx = context::create();
    auto const afb = acquire_full_buckets<int>(
        null_reader<int>(), sharable_new_delete_bucket_source<int>::create(),
        arg::batch_size<>{64}, ctx->tracker<acquire_access>("acq"),
        null_sink(), null_sink());
    auto const info = check_introspect_node_info(afb);
    auto const g = afb.introspect_graph();
    CHECK(g.nodes().size() == 3);
    CHECK(g.entry_points().size() == 1);
    auto const node = g.entry_points()[0];
    CHECK(g.node_info(node) == info);
    auto const edges = g.edges();
    CHECK(edges.size() == 2);
    CHECK(edges[0].first == node);
    CHECK(edges[1].first == node);
    CHECK(g.node_info(edges[0].second).name() == "null_sink");
    CHECK(g.node_info(edges[1].second).name() == "null_sink");
}

namespace {

struct mock_int_reader {
    using ret_type = std::optional<std::size_t>;
    // NOLINTNEXTLINE(modernize-use-trailing-return-type)
    MAKE_MOCK1(read, ret_type(span<int>));
    auto operator()(span<int> s) -> ret_type { return read(s); }
};

template <typename Reader> class ref_reader {
    Reader *r;

  public:
    explicit ref_reader(Reader &reader) : r(&reader) {}

    auto operator()(span<int> s) { return (*r)(s); }
};

constexpr auto ignore_value_category = feed_as::const_lvalue;

} // namespace

TEST_CASE("acquire") {
    auto ctx = context::create();
    auto reader = mock_int_reader();
    auto acq = acquire<int>(
        ref_reader(reader), new_delete_bucket_source<int>::create(),
        arg::batch_size<>{4}, ctx->tracker<acquire_access>("acq"),
        capture_output<type_list<bucket<int>>>(
            ctx->tracker<capture_output_access>("out")));
    auto out = capture_output_checker<type_list<bucket<int>>>(
        ignore_value_category, ctx, "out");
    auto acq_acc = ctx->access<acquire_access>("acq");

    using trompeloeil::_;

    SECTION("acquire processor is movable") {
        STATIC_CHECK(std::is_move_constructible_v<decltype(acq)> &&
                     std::is_move_assignable_v<decltype(acq)>);
    }

    SECTION("pre-halted acquisition never reads, immediately throws") {
        acq_acc.halt();
        CHECK_THROWS_AS(acq.flush(), acquisition_halted);
        CHECK(out.check_not_flushed());
    }

    SECTION("zero-length acquisition reads once and flushes") {
        REQUIRE_CALL(reader, read(_))
            .TIMES(1)
            .WITH(_1.size() == 4)
            .RETURN(std::nullopt);
        acq.flush();
        CHECK(out.check_flushed());
    }

    SECTION("read error propagates") {
        struct my_exception : std::exception {};
        REQUIRE_CALL(reader, read(_)).TIMES(1).THROW(my_exception());
        CHECK_THROWS_AS(acq.flush(), my_exception);
        CHECK(out.check_not_flushed());
    }

    SECTION("empty read does not emit bucket") {
        trompeloeil::sequence seq;
        REQUIRE_CALL(reader, read(_)).IN_SEQUENCE(seq).RETURN(0);
        REQUIRE_CALL(reader, read(_)).IN_SEQUENCE(seq).RETURN(std::nullopt);
        acq.flush();
        CHECK(out.check_flushed());
    }

    SECTION("partial-batch read emits bucket, pauses") {
        trompeloeil::sequence seq;
        REQUIRE_CALL(reader, read(_))
            .IN_SEQUENCE(seq)
            .SIDE_EFFECT(_1[0] = 42)
            .RETURN(1);
        REQUIRE_CALL(reader, read(_)).IN_SEQUENCE(seq).RETURN(std::nullopt);
        auto const start = std::chrono::steady_clock::now();
        acq.flush();
        auto const stop = std::chrono::steady_clock::now();
        CHECK(out.check(emitted_as::always_rvalue, test_bucket({42})));
        CHECK(out.check_flushed());
        // This may not actually test what we want to test on every execution,
        // but at least it shouldn't be flaky.
        CHECK(stop - start >= std::chrono::milliseconds(5));
    }

    SECTION("full-batch read emits bucket") {
        trompeloeil::sequence seq;
        REQUIRE_CALL(reader, read(_))
            .IN_SEQUENCE(seq)
            .SIDE_EFFECT(std::fill(_1.begin(), _1.end(), 42))
            .RETURN(_1.size());
        REQUIRE_CALL(reader, read(_)).IN_SEQUENCE(seq).RETURN(std::nullopt);
        // We do not check that we "do not pause", because that would be flaky.
        // To correctly test for that, we need to mock out the clock/sleep. Or
        // we could use a benchmark to ensure the average is fast.
        acq.flush();
        CHECK(out.check(emitted_as::always_rvalue,
                        test_bucket({42, 42, 42, 42})));
        CHECK(out.check_flushed());
    }

    SECTION("downstream exception propagates") {
        REQUIRE_CALL(reader, read(_))
            .TIMES(1)
            .SIDE_EFFECT(_1[0] = 42)
            .RETURN(1);
        out.throw_end_processing_on_next();
        CHECK_THROWS_AS(acq.flush(), end_of_processing);
    }

    SECTION("halt causes ongoing flush() to throw") {
        auto const read_size = GENERATE(std::size_t{0}, 4);
        ALLOW_CALL(reader, read(_)).RETURN(read_size);
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            wait_a_little();
            acq_acc.halt();
        });
        thread_start_latch.wait();
        CHECK_THROWS_AS(acq.flush(), acquisition_halted);
        t.join();
    }
}

TEST_CASE("acquire_full_buckets") {
    auto ctx = context::create();
    auto reader = mock_int_reader();
    auto acq = acquire_full_buckets<int>(
        ref_reader(reader), sharable_new_delete_bucket_source<int>::create(),
        arg::batch_size<>{4}, ctx->tracker<acquire_access>("acq"),
        capture_output<type_list<bucket<int const>>>(
            ctx->tracker<capture_output_access>("live")),
        capture_output<type_list<bucket<int>>>(
            ctx->tracker<capture_output_access>("batch")));
    auto live_out = capture_output_checker<type_list<bucket<int const>>>(
        ignore_value_category, ctx, "live");
    auto batch_out = capture_output_checker<type_list<bucket<int>>>(
        ignore_value_category, ctx, "batch");
    auto acq_acc = ctx->access<acquire_access>("acq");

    using trompeloeil::_;

    SECTION("acquire_full_buckets is movable") {
        STATIC_CHECK(std::is_move_constructible_v<decltype(acq)> &&
                     std::is_move_assignable_v<decltype(acq)>);
    }

    SECTION("pre-halted acquisition never reads, immediately throws") {
        acq_acc.halt();
        CHECK_THROWS_AS(acq.flush(), acquisition_halted);
        CHECK(live_out.check_not_flushed());
        CHECK(batch_out.check_not_flushed());
    }

    SECTION("zero-length acquisition reads once and flushes") {
        REQUIRE_CALL(reader, read(_))
            .TIMES(1)
            .WITH(_1.size() == 4)
            .RETURN(std::nullopt);
        acq.flush();
        CHECK(live_out.check_flushed());
        CHECK(batch_out.check_flushed());
    }

    SECTION("read error propagates") {
        struct my_exception : std::exception {};
        REQUIRE_CALL(reader, read(_)).TIMES(1).THROW(my_exception());
        CHECK_THROWS_AS(acq.flush(), my_exception);
        CHECK(live_out.check_not_flushed());
        CHECK(batch_out.check_not_flushed());
    }

    SECTION("empty read does not emit bucket") {
        trompeloeil::sequence seq;
        REQUIRE_CALL(reader, read(_)).IN_SEQUENCE(seq).RETURN(0);
        REQUIRE_CALL(reader, read(_)).IN_SEQUENCE(seq).RETURN(std::nullopt);
        acq.flush();
        CHECK(live_out.check_flushed());
        CHECK(batch_out.check_flushed());
    }

    SECTION("partial-batch reads emit buckets, pauses") {
        trompeloeil::sequence seq;
        REQUIRE_CALL(reader, read(_))
            .IN_SEQUENCE(seq)
            .WITH(_1.size() == 4)
            .SIDE_EFFECT(_1[0] = 42)
            .RETURN(1);
        REQUIRE_CALL(reader, read(_))
            .IN_SEQUENCE(seq)
            .WITH(_1.size() == 3)
            .SIDE_EFFECT(_1[0] = 42)
            .RETURN(1);
        REQUIRE_CALL(reader, read(_))
            .IN_SEQUENCE(seq)
            .WITH(_1.size() == 2)
            .RETURN(std::nullopt);

        auto const start = std::chrono::steady_clock::now();
        acq.flush();
        auto const stop = std::chrono::steady_clock::now();

        CHECK(live_out.check(emitted_as::always_rvalue,
                             test_bucket<int const>({42})));
        CHECK(live_out.check(emitted_as::always_rvalue,
                             test_bucket<int const>({42})));
        CHECK(live_out.check_flushed());
        // Remainder emitted as partial batch.
        CHECK(batch_out.check(emitted_as::always_rvalue,
                              test_bucket<int>({42, 42})));
        CHECK(batch_out.check_flushed());

        CHECK(stop - start >= std::chrono::milliseconds(5));
    }

    SECTION("full-batch read emits buckets") {
        trompeloeil::sequence seq;
        REQUIRE_CALL(reader, read(_))
            .IN_SEQUENCE(seq)
            .SIDE_EFFECT(std::fill(_1.begin(), _1.end(), 42))
            .RETURN(_1.size());
        REQUIRE_CALL(reader, read(_)).IN_SEQUENCE(seq).RETURN(std::nullopt);
        acq.flush();
        CHECK(live_out.check(emitted_as::always_rvalue,
                             test_bucket<int const>({42, 42, 42, 42})));
        CHECK(live_out.check_flushed());
        CHECK(batch_out.check(emitted_as::always_rvalue,
                              test_bucket<int>({42, 42, 42, 42})));
        CHECK(batch_out.check_flushed());
    }

    SECTION("live downstream throws on bucket") {
        REQUIRE_CALL(reader, read(_))
            .TIMES(1)
            .SIDE_EFFECT(_1[0] = 42)
            .RETURN(1);

        SECTION("end of processing") {
            live_out.throw_end_processing_on_next();
            CHECK_THROWS_AS(acq.flush(), end_of_processing);
            CHECK(
                batch_out.check(emitted_as::always_rvalue, test_bucket({42})));
            CHECK(batch_out.check_flushed());
        }

        SECTION("end of processing; batch downstream throws on flush") {
            live_out.throw_end_processing_on_next();
            SECTION("end of processing") {
                batch_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(acq.flush(), end_of_processing);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42})));
            }
            SECTION("error") {
                batch_out.throw_error_on_flush();
                CHECK_THROWS_AS(acq.flush(), test_error);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42})));
            }
        }

        SECTION("error") {
            live_out.throw_error_on_next();
            CHECK_THROWS_AS(acq.flush(), test_error);
            CHECK(batch_out.check_not_flushed());
        }
    }

    SECTION("live downstream throws on flush") {
        REQUIRE_CALL(reader, read(_)).TIMES(1).RETURN(std::nullopt);

        SECTION("end of processing") {
            live_out.throw_end_processing_on_flush();
            CHECK_THROWS_AS(acq.flush(), end_of_processing);
            CHECK(batch_out.check_flushed());
        }

        SECTION("end of processing; batch downstream throws on flush") {
            live_out.throw_end_processing_on_flush();
            SECTION("end of processing") {
                batch_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(acq.flush(), end_of_processing);
            }
            SECTION("error") {
                batch_out.throw_error_on_flush();
                CHECK_THROWS_AS(acq.flush(), test_error);
            }
        }

        SECTION("error") {
            live_out.throw_error_on_flush();
            CHECK_THROWS_AS(acq.flush(), test_error);
            CHECK(batch_out.check_not_flushed());
        }
    }

    SECTION("batch downstream throws on bucket") {
        REQUIRE_CALL(reader, read(_))
            .TIMES(1)
            .SIDE_EFFECT(std::fill(_1.begin(), _1.end(), 42))
            .RETURN(_1.size());

        SECTION("end of processing") {
            batch_out.throw_end_processing_on_next();
            CHECK_THROWS_AS(acq.flush(), end_of_processing);
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({42, 42, 42, 42})));
            CHECK(live_out.check_flushed());
        }

        SECTION("end of processing; live downstream throws on flush") {
            batch_out.throw_end_processing_on_next();
            SECTION("end of processing") {
                live_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(acq.flush(), end_of_processing);
                CHECK(
                    live_out.check(emitted_as::always_rvalue,
                                   test_bucket<int const>({42, 42, 42, 42})));
            }
            SECTION("error") {
                live_out.throw_error_on_flush();
                CHECK_THROWS_AS(acq.flush(), test_error);
                CHECK(
                    live_out.check(emitted_as::always_rvalue,
                                   test_bucket<int const>({42, 42, 42, 42})));
            }
        }

        SECTION("error") {
            batch_out.throw_error_on_next();
            CHECK_THROWS_AS(acq.flush(), test_error);
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({42, 42, 42, 42})));
            CHECK(live_out.check_not_flushed());
        }
    }

    SECTION("batch downstream throws on flush") {
        REQUIRE_CALL(reader, read(_)).TIMES(1).RETURN(std::nullopt);

        SECTION("end of processing") {
            batch_out.throw_end_processing_on_flush();
            CHECK_THROWS_AS(acq.flush(), end_of_processing);
            CHECK(live_out.check_flushed());
        }

        SECTION("end of processing; live downstream throws on flush") {
            batch_out.throw_end_processing_on_flush();
            SECTION("end of processing") {
                live_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(acq.flush(), end_of_processing);
            }
            SECTION("error") {
                live_out.throw_error_on_flush();
                CHECK_THROWS_AS(acq.flush(), test_error);
            }
        }

        SECTION("error") {
            batch_out.throw_error_on_flush();
            CHECK_THROWS_AS(acq.flush(), test_error);
            // Live downstream already flushed before batch downstream throws.
            CHECK(live_out.check_flushed());
        }
    }

    SECTION("halt causes ongoing flush() to throw") {
        auto const read_size = GENERATE(std::size_t{0}, 4);
        ALLOW_CALL(reader, read(_)).RETURN(read_size);
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            wait_a_little();
            acq_acc.halt();
        });
        thread_start_latch.wait();
        CHECK_THROWS_AS(acq.flush(), acquisition_halted);
        t.join();
    }
}

} // namespace tcspc
