/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/gate.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

using namespace tcspc;

using open_event = empty_test_event<0>;
using close_event = empty_test_event<1>;
using gated_event = empty_test_event<2>;
using misc_event = empty_test_event<3>;

TEST_CASE("Gate events", "[gate]") {
    bool const initially_open = GENERATE(false, true);
    auto out = capture_output<
        event_set<open_event, close_event, gated_event, misc_event>>();
    auto in = feed_input<
        event_set<open_event, close_event, gated_event, misc_event>>(
        gate<event_set<gated_event>, open_event, close_event>(
            initially_open, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Initial state") {
        in.feed(gated_event{});
        if (initially_open)
            REQUIRE(out.check(gated_event{}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Pass through unrelated events") {
        in.feed(misc_event{});
        REQUIRE(out.check(misc_event{}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Pass through open/close") {
        in.feed(open_event{});
        REQUIRE(out.check(open_event{}));
        in.feed(close_event{});
        REQUIRE(out.check(close_event{}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Gate closed") {
        in.feed(close_event{});
        REQUIRE(out.check(close_event{}));
        in.feed(gated_event{});
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Gate open") {
        in.feed(open_event{});
        REQUIRE(out.check(open_event{}));
        in.feed(gated_event{});
        REQUIRE(out.check(gated_event{}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}
