/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/delay_hasten.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <exception>
#include <stdexcept>

#include <catch2/catch_all.hpp>

using namespace tcspc;

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;
using e2 = timestamped_test_event<2>;
using e3 = timestamped_test_event<3>;

TEST_CASE("Delay uniform streams", "[delay_processor]") {
    macrotime const delta = GENERATE(0, 1, 2);
    auto out = capture_output<event_set<e0, e1, e2, e3>>();
    auto in = feed_input<event_set<e0, e1, e2, e3>>(
        delay_processor<event_set<e0, e1>>(delta, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Empty stream yields empty stream") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        in.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
    }

    SECTION("Undelayed events are unbuffered") {
        in.feed(e2{0});
        REQUIRE(out.check(e2{0}));
        in.feed(e3{0});
        REQUIRE(out.check(e3{0}));
        in.feed(e2{0});
        REQUIRE(out.check(e2{0}));
        in.feed(e3{0});
        REQUIRE(out.check(e3{0}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Delayed events are buffered") {
        in.feed(e0{0});
        in.feed(e1{0});
        in.feed(e0{0});
        in.feed(e1{0});
        in.feed_end();
        REQUIRE(out.check(e0{delta}));
        REQUIRE(out.check(e1{delta}));
        REQUIRE(out.check(e0{delta}));
        REQUIRE(out.check(e1{delta}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Hasten uniform streams", "[hasten_processor]") {
    macrotime const delta = GENERATE(0, 1, 2);
    auto out = capture_output<event_set<e0, e1, e2, e3>>();
    auto in = feed_input<event_set<e0, e1, e2, e3>>(
        hasten_processor<event_set<e2, e3>>(delta, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Empty stream yields empty stream") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        in.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
    }

    SECTION("Hastened events are unbuffered") {
        in.feed(e0{0});
        REQUIRE(out.check(e0{-delta}));
        in.feed(e1{0});
        REQUIRE(out.check(e1{-delta}));
        in.feed(e0{0});
        REQUIRE(out.check(e0{-delta}));
        in.feed(e1{0});
        REQUIRE(out.check(e1{-delta}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Unhastened events are buffered") {
        in.feed(e2{0});
        in.feed(e3{0});
        in.feed(e2{0});
        in.feed(e3{0});
        in.feed_end();
        REQUIRE(out.check(e2{0}));
        REQUIRE(out.check(e3{0}));
        REQUIRE(out.check(e2{0}));
        REQUIRE(out.check(e3{0}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Delay by 0", "[delay_processor]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        delay_processor<event_set<e0>>(0, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Equal timestamps") {
        in.feed(e0{0});
        in.feed(e1{0});
        REQUIRE(out.check(e0{0}));
        REQUIRE(out.check(e1{0}));
        in.feed(e0{0});
        in.feed(e1{0});
        REQUIRE(out.check(e0{0}));
        REQUIRE(out.check(e1{0}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Increment of 1") {
        in.feed(e0{0});
        in.feed(e1{1});
        REQUIRE(out.check(e0{0}));
        REQUIRE(out.check(e1{1}));
        in.feed(e0{2});
        in.feed(e1{3});
        REQUIRE(out.check(e0{2}));
        REQUIRE(out.check(e1{3}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Hasten by 0", "[hasten_processor]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        hasten_processor<event_set<e1>>(0, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Equal timestamps") {
        in.feed(e1{0});
        in.feed(e0{0});
        REQUIRE(out.check(e0{0}));
        in.feed(e1{0});
        in.feed(e0{0});
        REQUIRE(out.check(e0{0}));
        in.feed_end();
        REQUIRE(out.check(e1{0}));
        REQUIRE(out.check(e1{0}));
        REQUIRE(out.check_end());
    }

    SECTION("Increment of 1") {
        in.feed(e1{0});
        in.feed(e0{1});
        REQUIRE(out.check(e1{0}));
        REQUIRE(out.check(e0{1}));
        in.feed(e1{2});
        in.feed(e0{3});
        REQUIRE(out.check(e1{2}));
        REQUIRE(out.check(e0{3}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Delay by 1", "[delay_processor]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        delay_processor<event_set<e0>>(1, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Equal timestamps") {
        in.feed(e0{0});
        in.feed(e1{0});
        REQUIRE(out.check(e1{0}));
        in.feed(e0{1});
        in.feed(e1{1});
        REQUIRE(out.check(e0{1}));
        REQUIRE(out.check(e1{1}));
        in.feed_end();
        REQUIRE(out.check(e0{2}));
        REQUIRE(out.check_end());
    }

    SECTION("Increment of 1") {
        in.feed(e0{0});
        in.feed(e1{1});
        REQUIRE(out.check(e0{1}));
        REQUIRE(out.check(e1{1}));
        in.feed(e0{2});
        in.feed(e1{3});
        REQUIRE(out.check(e0{3}));
        REQUIRE(out.check(e1{3}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Hasten by 1", "[hasten_processor]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        hasten_processor<event_set<e1>>(1, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Equal timestamps") {
        in.feed(e1{0});
        in.feed(e0{0});
        REQUIRE(out.check(e0{-1}));
        in.feed(e1{1});
        in.feed(e0{1});
        REQUIRE(out.check(e0{0}));
        in.feed_end();
        REQUIRE(out.check(e1{0}));
        REQUIRE(out.check(e1{1}));
        REQUIRE(out.check_end());
    }

    SECTION("Increment of 1") {
        in.feed(e1{0});
        in.feed(e0{1});
        REQUIRE(out.check(e0{0}));
        in.feed(e1{2});
        in.feed(e0{3});
        REQUIRE(out.check(e1{0}));
        REQUIRE(out.check(e0{2}));
        in.feed_end();
        REQUIRE(out.check(e1{2}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Delay by 2", "[delay_processor]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        delay_processor<event_set<e0>>(2, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Equal timestamps") {
        in.feed(e0{0});
        in.feed(e1{0});
        REQUIRE(out.check(e1{0}));
        in.feed(e0{1});
        in.feed(e1{1});
        REQUIRE(out.check(e1{1}));
        in.feed(e0{2});
        in.feed(e1{2});
        REQUIRE(out.check(e0{2}));
        REQUIRE(out.check(e1{2}));
        in.feed(e1{3});
        REQUIRE(out.check(e0{3}));
        REQUIRE(out.check(e1{3}));
        in.feed_end();
        REQUIRE(out.check(e0{4}));
        REQUIRE(out.check_end());
    }

    SECTION("Increment of 1") {
        in.feed(e0{0});
        in.feed(e1{1});
        REQUIRE(out.check(e1{1}));
        in.feed(e0{2});
        in.feed(e1{3});
        REQUIRE(out.check(e0{2}));
        REQUIRE(out.check(e1{3}));
        in.feed(e0{4});
        in.feed(e1{5});
        REQUIRE(out.check(e0{4}));
        REQUIRE(out.check(e1{5}));
        in.feed_end();
        REQUIRE(out.check(e0{6}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Hasten by 2", "[hasten_processor]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        hasten_processor<event_set<e1>>(2, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Equal timestamps") {
        in.feed(e1{0});
        in.feed(e0{0});
        REQUIRE(out.check(e0{-2}));
        in.feed(e1{1});
        in.feed(e0{1});
        REQUIRE(out.check(e0{-1}));
        in.feed(e1{2});
        in.feed(e0{2});
        REQUIRE(out.check(e0{0}));
        in.feed(e0{3});
        REQUIRE(out.check(e1{0}));
        REQUIRE(out.check(e0{1}));
        in.feed_end();
        REQUIRE(out.check(e1{1}));
        REQUIRE(out.check(e1{2}));
        REQUIRE(out.check_end());
    }

    SECTION("Increment of 1") {
        in.feed(e1{0});
        in.feed(e0{1});
        REQUIRE(out.check(e0{-1}));
        in.feed(e1{2});
        in.feed(e0{3});
        REQUIRE(out.check(e1{0}));
        REQUIRE(out.check(e0{1}));
        in.feed(e1{4});
        in.feed(e0{5});
        REQUIRE(out.check(e1{2}));
        REQUIRE(out.check(e0{3}));
        in.feed_end();
        REQUIRE(out.check(e1{4}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Delay-hasten basic test", "[delay_hasten_processor]") {
    macrotime const delta = GENERATE(-2, -1, 0, 1, 2);
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        delay_hasten_processor<event_set<e0>, event_set<e1>>(
            delta, ref_processor(out)));
    in.require_output_checked(out);

    // N.B. Because of the extraneous 0-hasten or 0-delay processor, the
    // buffering behavior differs from a separate delay_processor or
    // hasten_processor. This test is therefore overconstrained.

    in.feed(e1{-3});
    in.feed(e0{0});
    in.feed(e1{3});
    REQUIRE(out.check(e1{-3}));
    REQUIRE(out.check(e0{0 + delta}));
    in.feed(e0{6});
    in.feed_end();
    REQUIRE(out.check(e1{3}));
    REQUIRE(out.check(e0{6 + delta}));
    REQUIRE(out.check_end());
}
