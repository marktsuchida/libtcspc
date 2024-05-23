/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/copy_to_buckets.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/introspect.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace tcspc {

namespace {

using misc_event = empty_test_event<0>;

} // namespace

TEST_CASE("type constraints: copy_to_buckets") {
    using proc_type =
        decltype(copy_to_buckets<int>(new_delete_bucket_source<int>::create(),
                                      sink_events<bucket<int>, misc_event>()));
    STATIC_CHECK(is_processor_v<proc_type, span<int const>, misc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);

    STATIC_CHECK(handles_event_v<proc_type, span<int>>);
    STATIC_CHECK(handles_event_v<proc_type, bucket<int>>);
    STATIC_CHECK(handles_event_v<proc_type, bucket<int const>>);
    STATIC_CHECK(handles_event_v<proc_type, std::vector<int>>);
}

TEST_CASE("type constraints: copy_to_full_buckets") {
    using proc_type = decltype(copy_to_full_buckets<int>(
        sharable_new_delete_bucket_source<int>::create(),
        arg::batch_size<std::size_t>{64},
        sink_events<bucket<int const>, misc_event>(),
        sink_events<bucket<int>>()));
    STATIC_CHECK(is_processor_v<proc_type, span<int const>, misc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);

    STATIC_CHECK(handles_event_v<proc_type, span<int>>);
    STATIC_CHECK(handles_event_v<proc_type, bucket<int>>);
    STATIC_CHECK(handles_event_v<proc_type, bucket<int const>>);
    STATIC_CHECK(handles_event_v<proc_type, std::vector<int>>);
}

TEST_CASE("introspect: copy_to_buckets") {
    check_introspect_simple_processor(copy_to_buckets<int>(
        new_delete_bucket_source<int>::create(), null_sink()));
}

TEST_CASE("introspect: copy_to_full_buckets") {
    auto const ctfb = copy_to_full_buckets<int>(
        sharable_new_delete_bucket_source<int>::create(),
        arg::batch_size<std::size_t>{64}, null_sink(), null_sink());
    auto const info = check_introspect_node_info(ctfb);
    auto const g = ctfb.introspect_graph();
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

TEST_CASE("copy_to_buckets") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<int>::create(
        new_delete_bucket_source<int>::create(), 42);
    auto in = feed_input(
        valcat,
        copy_to_buckets<int>(
            bsource, capture_output<type_list<bucket<int>, misc_event>>(
                         ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<bucket<int>, misc_event>>(
        valcat, ctx, "out");

    SECTION("non-span event is passed through") {
        in.handle(misc_event{});
        CHECK(out.check(emitted_as::same_as_fed, misc_event{}));
    }

    SECTION("span is copied, even if given as bucket handled by downstream") {
        CHECK(bsource->bucket_count() == 0);
        auto input = test_bucket({42, 43, 44});
        auto const *p_input = input.data();
        in.handle(std::move(input));
        auto const output = out.pop<bucket<int>>(emitted_as::always_rvalue);
        CHECK(output == test_bucket({42, 43, 44}));
        CHECK(output.data() != p_input);
        CHECK(bsource->bucket_count() == 1);
    }

    in.flush();
    CHECK(out.check_flushed());
}

TEST_CASE("copy_to_full_buckets") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<int>::create(
        sharable_new_delete_bucket_source<int>::create(), 42);
    auto in = feed_input(
        valcat, copy_to_full_buckets<int>(
                    bsource, arg::batch_size<std::size_t>{4},
                    capture_output<type_list<bucket<int const>, misc_event>>(
                        ctx->tracker<capture_output_access>("live")),
                    capture_output<type_list<bucket<int>>>(
                        ctx->tracker<capture_output_access>("batch"))));
    in.require_output_checked(ctx, "live");
    in.require_output_checked(ctx, "batch");
    auto live_out =
        capture_output_checker<type_list<bucket<int const>, misc_event>>(
            valcat, ctx, "live");
    auto batch_out =
        capture_output_checker<type_list<bucket<int>>>(valcat, ctx, "batch");

    SECTION("non-span event is passed through only to live downstream") {
        in.handle(misc_event{});
        CHECK(live_out.check(emitted_as::same_as_fed, misc_event{}));
        in.flush();
        CHECK(live_out.check_flushed());
        CHECK(batch_out.check_flushed());
    }

    SECTION("empty read emits nothing") {
        in.handle(span<int>());
        in.flush();
        CHECK(live_out.check_flushed());
        CHECK(batch_out.check_flushed());
        CHECK(bsource->bucket_count() == 0);
    }

    SECTION("complete batch is emitted to both downstreams") {
        in.handle(test_bucket({42, 43, 44, 45}));
        CHECK(live_out.check(test_bucket<int const>({42, 43, 44, 45})));
        CHECK(batch_out.check(test_bucket({42, 43, 44, 45})));

        SECTION("end") {
            in.flush();
            CHECK(live_out.check_flushed());
            CHECK(batch_out.check_flushed());
            CHECK(bsource->bucket_count() == 1);
        }

        SECTION("2-batch span") {
            in.handle(test_bucket({46, 47, 48, 49, 50, 51, 52, 53}));
            CHECK(live_out.check(test_bucket<int const>({46, 47, 48, 49})));
            CHECK(live_out.check(test_bucket<int const>({50, 51, 52, 53})));
            CHECK(batch_out.check(test_bucket({46, 47, 48, 49})));
            CHECK(batch_out.check(test_bucket({50, 51, 52, 53})));
            in.flush();
            CHECK(live_out.check_flushed());
            CHECK(batch_out.check_flushed());
            CHECK(bsource->bucket_count() == 3);
        }
    }

    SECTION("partial batch is emitted only to live downstream") {
        in.handle(test_bucket({42, 43, 44}));
        CHECK(live_out.check(test_bucket<int const>({42, 43, 44})));

        SECTION("end partial") {
            in.flush();
            CHECK(live_out.check_flushed());
            CHECK(batch_out.check(test_bucket({42, 43, 44})));
            CHECK(batch_out.check_flushed());
            CHECK(bsource->bucket_count() == 1);
        }

        SECTION("batch completed exactly") {
            in.handle(test_bucket({45}));
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({45})));
            CHECK(batch_out.check(emitted_as::always_rvalue,
                                  test_bucket({42, 43, 44, 45})));
            in.flush();
            CHECK(live_out.check_flushed());
            CHECK(batch_out.check_flushed());
            CHECK(bsource->bucket_count() == 1);
        }

        SECTION("2 batches spanned, both partial") {
            in.handle(test_bucket({45, 46}));
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({45})));
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({46})));
            CHECK(batch_out.check(emitted_as::always_rvalue,
                                  test_bucket({42, 43, 44, 45})));
            in.flush();
            CHECK(live_out.check_flushed());
            CHECK(batch_out.check(test_bucket({46})));
            CHECK(batch_out.check_flushed());
            CHECK(bsource->bucket_count() == 2);
        }

        SECTION("3 batches spanned") {
            in.handle(test_bucket({45, 46, 47, 48, 49, 50}));
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({45})));
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({46, 47, 48, 49})));
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({50})));
            CHECK(batch_out.check(emitted_as::always_rvalue,
                                  test_bucket({42, 43, 44, 45})));
            CHECK(batch_out.check(emitted_as::always_rvalue,
                                  test_bucket({46, 47, 48, 49})));
            in.flush();
            CHECK(live_out.check_flushed());
            CHECK(batch_out.check(test_bucket({50})));
            CHECK(batch_out.check_flushed());
            CHECK(bsource->bucket_count() == 3);
        }
    }

    SECTION("live downstream throws on non-span event") {
        // Test with partial batch pending to ensure it is flushed.
        in.handle(test_bucket({42, 43}));
        CHECK(live_out.check(emitted_as::always_rvalue,
                             test_bucket<int const>({42, 43})));

        SECTION("end of processing") {
            live_out.throw_end_processing_on_next();
            CHECK_THROWS_AS(in.handle(misc_event{}), end_of_processing);
            CHECK(batch_out.check(emitted_as::always_rvalue,
                                  test_bucket({42, 43})));
            CHECK(batch_out.check_flushed());
        }

        SECTION("end of processing; batch downstream throws on flush") {
            live_out.throw_end_processing_on_next();
            SECTION("end of processing") {
                batch_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(in.handle(misc_event{}), end_of_processing);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42, 43})));
            }
            SECTION("error") {
                batch_out.throw_error_on_flush();
                CHECK_THROWS_AS(in.handle(misc_event{}), test_error);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42, 43})));
                CHECK(batch_out.check_not_flushed());
            }
        }

        SECTION("error") {
            live_out.throw_error_on_next();
            CHECK_THROWS_AS(in.handle(misc_event{}), test_error);
            CHECK(batch_out.check_not_flushed());
        }
    }

    SECTION("live downstream throws on bucket") {
        in.handle(test_bucket({42, 43}));
        CHECK(live_out.check(emitted_as::always_rvalue,
                             test_bucket<int const>({42, 43})));

        SECTION("end of processing") {
            live_out.throw_end_processing_on_next();
            CHECK_THROWS_AS(in.handle(test_bucket({44})), end_of_processing);
            CHECK(batch_out.check(emitted_as::always_rvalue,
                                  test_bucket({42, 43, 44})));
            CHECK(batch_out.check_flushed());
        }

        SECTION("end of processing; batch downstream throws on flush") {
            live_out.throw_end_processing_on_next();
            SECTION("end of processing") {
                batch_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(in.handle(test_bucket({44})),
                                end_of_processing);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42, 43, 44})));
            }
            SECTION("error") {
                batch_out.throw_error_on_flush();
                CHECK_THROWS_AS(in.handle(test_bucket({44})), test_error);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42, 43, 44})));
                CHECK(batch_out.check_not_flushed());
            }
        }

        SECTION("error") {
            live_out.throw_error_on_next();
            CHECK_THROWS_AS(in.handle(test_bucket({44})), test_error);
            CHECK(batch_out.check_not_flushed());
        }
    }

    SECTION("live downstream throws on flush") {
        in.handle(test_bucket({42, 43}));
        CHECK(live_out.check(emitted_as::always_rvalue,
                             test_bucket<int const>({42, 43})));

        SECTION("end of processing") {
            live_out.throw_end_processing_on_flush();
            CHECK_THROWS_AS(in.flush(), end_of_processing);
            CHECK(batch_out.check(emitted_as::always_rvalue,
                                  test_bucket({42, 43})));
            CHECK(batch_out.check_flushed());
        }

        SECTION("end of processing; batch downstream throws on flush") {
            live_out.throw_end_processing_on_flush();
            SECTION("end of processing") {
                batch_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(in.flush(), end_of_processing);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42, 43})));
            }
            SECTION("error") {
                batch_out.throw_error_on_flush();
                CHECK_THROWS_AS(in.flush(), test_error);
                CHECK(batch_out.check(emitted_as::always_rvalue,
                                      test_bucket({42, 43})));
                CHECK(batch_out.check_not_flushed());
            }
        }

        SECTION("error") {
            live_out.throw_error_on_flush();
            CHECK_THROWS_AS(in.flush(), test_error);
            CHECK(batch_out.check_not_flushed());
        }
    }

    SECTION("batch downstream throws on bucket") {
        SECTION("end of processing") {
            batch_out.throw_end_processing_on_next();
            CHECK_THROWS_AS(in.handle(test_bucket({42, 43, 44, 45})),
                            end_of_processing);
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({42, 43, 44, 45})));
            CHECK(live_out.check_flushed());
        }

        SECTION("end of processing; live downstream throws on flush") {
            batch_out.throw_end_processing_on_next();
            SECTION("end of processing") {
                live_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(in.handle(test_bucket({42, 43, 44, 45})),
                                end_of_processing);
                CHECK(
                    live_out.check(emitted_as::always_rvalue,
                                   test_bucket<int const>({42, 43, 44, 45})));
            }
            SECTION("error") {
                live_out.throw_error_on_flush();
                CHECK_THROWS_AS(in.handle(test_bucket({42, 43, 44, 45})),
                                test_error);
                CHECK(
                    live_out.check(emitted_as::always_rvalue,
                                   test_bucket<int const>({42, 43, 44, 45})));
                CHECK(live_out.check_not_flushed());
            }
        }

        SECTION("error") {
            batch_out.throw_error_on_next();
            CHECK_THROWS_AS(in.handle(test_bucket({42, 43, 44, 45})),
                            test_error);
            CHECK(live_out.check(emitted_as::always_rvalue,
                                 test_bucket<int const>({42, 43, 44, 45})));
            CHECK(live_out.check_not_flushed());
        }
    }

    SECTION("batch downstream throws on flush") {
        SECTION("end of processing") {
            batch_out.throw_end_processing_on_flush();
            CHECK_THROWS_AS(in.flush(), end_of_processing);
            CHECK(live_out.check_flushed());
        }

        SECTION("end of processing; live downstream throws on flush") {
            batch_out.throw_end_processing_on_flush();
            SECTION("end of processing") {
                live_out.throw_end_processing_on_flush();
                CHECK_THROWS_AS(in.flush(), end_of_processing);
            }
            SECTION("error") {
                live_out.throw_error_on_flush();
                CHECK_THROWS_AS(in.flush(), test_error);
            }
        }

        SECTION("error") {
            batch_out.throw_error_on_flush();
            CHECK_THROWS_AS(in.flush(), test_error);
            // Live downstream already flushed before batch downstream throws.
            CHECK(live_out.check_flushed());
        }
    }
}

} // namespace tcspc
