/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/merge.hpp"

#include "flimevt/ref_processor.hpp"
#include "flimevt/test_utils.hpp"
#include "flimevt/type_erased_processor.hpp"

#include <catch2/catch_all.hpp>

using namespace flimevt;

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;
using e2 = timestamped_test_event<2>;
using e3 = timestamped_test_event<3>;
using all_events = event_set<e0, e1, e2, e3>;

TEST_CASE("Merge with error on one input", "[merge]") {
    // The two merge inputs have different types. Wrap them so they can be
    // swapped for symmetric tests.
    // in_0 -> ref -> min0 -> merge_impl -> out
    // in_x -> min_x -> rmin0/1 -> min0/1 -> merge_impl -> out
    //         erased    ref

    auto out = capture_output<all_events>();
    auto [min0, min1] = merge<all_events>(1000, ref_processor(out));

    auto rmin0 = ref_processor(min0);
    auto rmin1 = ref_processor(min1);
    int const x = GENERATE(0, 1);
    auto min_x = x != 0 ? type_erased_processor<all_events>(std::move(rmin1))
                        : type_erased_processor<all_events>(std::move(rmin0));
    auto min_y = x != 0 ? type_erased_processor<all_events>(std::move(rmin0))
                        : type_erased_processor<all_events>(std::move(rmin1));

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

    SECTION("Events in macrotime order") {
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
    auto [min0, min1] = merge<all_events>(10, ref_processor(out));

    int const x = GENERATE(0, 1);
    auto min_x = x != 0 ? type_erased_processor<all_events>(std::move(min1))
                        : type_erased_processor<all_events>(std::move(min0));
    auto min_y = x != 0 ? type_erased_processor<all_events>(std::move(min0))
                        : type_erased_processor<all_events>(std::move(min1));

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
