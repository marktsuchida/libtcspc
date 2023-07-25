/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/merge.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_erased_processor.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;
using e2 = timestamped_test_event<2>;
using e3 = timestamped_test_event<3>;
using all_events = event_set<e0, e1, e2, e3>;

TEST_CASE("Merge with error on one input", "[merge]") {
    // The two merge inputs have different types. Wrap them so they can be
    // swapped for symmetric tests.
    // in_0 -> ref -> min0 -> merge_impl -> out
    // in_x -> min_x -> ref -> min0/1 -> merge_impl -> out
    //         erased

    auto out = capture_output<all_events>();
    auto [min0, min1] =
        merge<default_data_traits, all_events>(1000, ref_processor(out));

    auto min_x = type_erased_processor<all_events>(ref_processor(min0));
    auto min_y = type_erased_processor<all_events>(ref_processor(min1));
    int const x = GENERATE(0, 1);
    if (x != 0) {
        using std::swap;
        swap(min_x, min_y);
    }

    auto in_0 = feed_input<all_events>(ref_processor(min0));
    in_0.require_output_checked(out);
    auto in_1 = feed_input<all_events>(ref_processor(min1));
    in_1.require_output_checked(out);
    auto in_x = feed_input<all_events>(std::move(min_x));
    in_x.require_output_checked(out);
    auto in_y = feed_input<all_events>(std::move(min_y));
    in_y.require_output_checked(out);

    SECTION("Error on in_x with no pending events") {
        in_x.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);

        // Other input must accept up to end of stream, but not emit anything

        SECTION("No further input") {
            in_y.feed_end();
            REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
        }

        SECTION("Further input on in_y") {
            in_y.feed(e2{});
            in_y.feed_end();
            REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
        }
    }

    SECTION("Error on in_x with events pending on in_x") {
        in_x.feed(e0{0});
        REQUIRE(out.check_not_end());
        in_x.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        // Pending events are discarded
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);

        // Other input must accept up to end of stream, but not emit anything

        SECTION("No further input") {
            in_y.feed_end();
            REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
        }

        SECTION("Further input on in_y") {
            in_y.feed(e2{});
            in_y.feed_end();
            REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
        }
    }

    SECTION("Error on in_x with events pending on in_y") {
        in_y.feed(e0{0});
        REQUIRE(out.check_not_end());
        in_x.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        // Pending events are discarded
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);

        // Other input must accept up to end of stream, but not emit anything

        SECTION("No further input") {
            in_y.feed_end();
            REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
        }

        SECTION("Further input on in_y") {
            in_y.feed(e2{});
            in_y.feed_end();
            REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
        }
    }

    SECTION("Empty yields empty") {
        in_x.feed_end();
        REQUIRE(out.check_not_end());
        in_y.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Error on both inputs") {
        in_x.feed_end(std::make_exception_ptr(std::runtime_error("test_x")));
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
        in_y.feed_end(std::make_exception_ptr(std::runtime_error("test_y")));
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
    }

    SECTION("Events from in_0 emitted before those from in_1") {
        in_1.feed(e1{42});
        in_0.feed(e0{42});
        REQUIRE(out.check(e0{42}));
        in_1.feed(e3{42});
        in_0.feed(e2{42});
        REQUIRE(out.check(e2{42}));

        SECTION("End in_0 first") {
            in_0.feed_end();
            REQUIRE(out.check(e1{42}));
            REQUIRE(out.check(e3{42}));
            REQUIRE(out.check_not_end());
            in_1.feed_end();
            REQUIRE(out.check_end());
        }

        SECTION("End in_1 first") {
            in_1.feed_end();
            REQUIRE(out.check_not_end());
            in_0.feed_end();
            REQUIRE(out.check(e1{42}));
            REQUIRE(out.check(e3{42}));
            REQUIRE(out.check_end());
        }
    }

    SECTION("Events in abstime order") {
        in_x.feed(e0{1});
        in_y.feed(e1{2});
        REQUIRE(out.check(e0{1}));
        in_x.feed(e0{3});
        REQUIRE(out.check(e1{2}));

        SECTION("End in_x first") {
            in_x.feed_end();
            REQUIRE(out.check_not_end());
            in_y.feed_end();
            REQUIRE(out.check(e0{3}));
            REQUIRE(out.check_end());
        }

        SECTION("End in_y first") {
            in_y.feed_end();
            REQUIRE(out.check(e0{3}));
            REQUIRE(out.check_not_end());
            in_x.feed_end();
            REQUIRE(out.check_end());
        }
    }

    SECTION("Delayed on in_x") {
        in_x.feed(e0{2});
        in_y.feed(e1{1});
        REQUIRE(out.check(e1{1}));
        in_x.feed(e0{4});
        in_y.feed(e1{3});
        REQUIRE(out.check(e0{2}));
        REQUIRE(out.check(e1{3}));

        SECTION("End in_x first") {
            in_x.feed_end();
            REQUIRE(out.check_not_end());
            in_y.feed_end();
            REQUIRE(out.check(e0{4}));
            REQUIRE(out.check_end());
        }

        SECTION("End in_y first") {
            in_y.feed_end();
            REQUIRE(out.check(e0{4}));
            REQUIRE(out.check_not_end());
            in_x.feed_end();
            REQUIRE(out.check_end());
        }
    }
}

TEST_CASE("Merge max time shift", "[merge]") {
    // in_x -> min_x -> min0 -> merge_impl -> out
    //         erased

    auto out = capture_output<all_events>();
    auto [min0, min1] =
        merge<default_data_traits, all_events>(10, ref_processor(out));

    int const x = GENERATE(0, 1);
    auto min_x = type_erased_processor<all_events>(std::move(min0));
    auto min_y = type_erased_processor<all_events>(std::move(min1));
    if (x != 0) {
        using std::swap;
        swap(min_x, min_y);
    }

    auto in_x = feed_input<all_events>(std::move(min_x));
    in_x.require_output_checked(out);
    auto in_y = feed_input<all_events>(std::move(min_y));
    in_y.require_output_checked(out);

    SECTION("in_x emitted after exceeding max time shift") {
        in_x.feed(e0{0});
        in_x.feed(e0{10});
        in_x.feed(e0{11});
        REQUIRE(out.check(e0{0}));

        SECTION("End in_x first") {
            in_x.feed_end();
            REQUIRE(out.check_not_end());
            in_y.feed_end();
            REQUIRE(out.check(e0{10}));
            REQUIRE(out.check(e0{11}));
            REQUIRE(out.check_end());
        }

        SECTION("End in_y first") {
            in_y.feed_end();
            REQUIRE(out.check(e0{10}));
            REQUIRE(out.check(e0{11}));
            REQUIRE(out.check_not_end());
            in_x.feed_end();
            REQUIRE(out.check_end());
        }
    }
}

TEST_CASE("merge N streams", "[merge_n]") {
    auto out = capture_output<all_events>();

    SECTION("Zero-stream merge_n returns empty tuple") {
        // NOLINTBEGIN(clang-analyzer-deadcode.DeadStores)
        auto tup = merge_n<0, default_data_traits, all_events>(
            1000, ref_processor(out));
        static_assert(std::tuple_size_v<decltype(tup)> == 0);
        // NOLINTEND(clang-analyzer-deadcode.DeadStores)
    }

    SECTION("Single-stream merge_n returns downstream in tuple") {
        auto [m0] = merge_n<1, default_data_traits, all_events>(
            1000, ref_processor(out));
        static_assert(
            std::is_same_v<decltype(m0), decltype(ref_processor(out))>);
        // clang-tidy bug? std::move() is necessary here.
        // NOLINTNEXTLINE(performance-move-const-arg)
        auto in = feed_input<all_events>(std::move(m0));
        in.require_output_checked(out);
        in.feed(e0{0});
        REQUIRE(out.check(e0{0}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Multi-stream merge_n can be instantiated") {
        auto [m0, m1] = merge_n<2, default_data_traits, all_events>(
            1000, ref_processor(out));
        auto [n0, n1, n2] = merge_n<3, default_data_traits, all_events>(
            1000, ref_processor(out));
        auto [o0, o1, o2, o3] = merge_n<4, default_data_traits, all_events>(
            1000, ref_processor(out));
        auto [p0, p1, p2, p3, p4] =
            merge_n<5, default_data_traits, all_events>(1000,
                                                        ref_processor(out));
    }
}

} // namespace tcspc
