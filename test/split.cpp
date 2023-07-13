/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/split.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;
using e2 = empty_test_event<2>;
using e3 = empty_test_event<3>;

TEST_CASE("Split events", "[split]") {
    auto out0 = capture_output<event_set<e0, e1>>();
    auto out1 = capture_output<event_set<e2, e3>>();
    auto in = feed_input<event_set<e0, e1, e2, e3>>(
        split<event_set<e2, e3>>(ref_processor(out0), ref_processor(out1)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);

    SECTION("Empty stream yields empty streams") {
        in.feed_end();
        REQUIRE(out0.check_end());
        REQUIRE(out1.check_end());
    }

    SECTION("Errors propagate to both streams") {
        in.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE_THROWS_AS(out0.check_end(), std::runtime_error);
        REQUIRE_THROWS_AS(out1.check_end(), std::runtime_error);
    }

    SECTION("Events are split") {
        in.feed(e0{});
        REQUIRE(out0.check(e0{}));
        in.feed(e2{});
        REQUIRE(out1.check(e2{}));
        in.feed_end();
        REQUIRE(out0.check_end());
        REQUIRE(out1.check_end());
    }
}

TEST_CASE("Split events, empty on out0", "[split]") {
    auto out0 = capture_output<event_set<>>();
    auto out1 = capture_output<event_set<e0>>();
    auto in = feed_input<event_set<e0>>(
        split<event_set<e0>>(ref_processor(out0), ref_processor(out1)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);

    in.feed(e0{});
    REQUIRE(out1.check(e0{}));
    in.feed_end();
    REQUIRE(out0.check_end());
    REQUIRE(out1.check_end());
}

TEST_CASE("Split events, empty on out1", "[split]") {
    auto out0 = capture_output<event_set<e0>>();
    auto out1 = capture_output<event_set<>>();
    auto in = feed_input<event_set<e0>>(
        split<event_set<>>(ref_processor(out0), ref_processor(out1)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);

    in.feed(e0{});
    REQUIRE(out0.check(e0{}));
    in.feed_end();
    REQUIRE(out0.check_end());
    REQUIRE(out1.check_end());
}

} // namespace tcspc
