/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/route.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"

#include <catch2/catch_all.hpp>

using namespace tcspc;

using tc_event = time_correlated_detection_event;

TEST_CASE("Route", "[route]") {
    auto out0 = capture_output<event_set<tc_event, marker_event>>();
    auto out1 = capture_output<event_set<tc_event, marker_event>>();
    auto out2 = capture_output<event_set<tc_event, marker_event>>();
    auto in = feed_input<event_set<tc_event, marker_event>>(
        route<event_set<tc_event>>(
            channel_router(std::array<std::int16_t, 3>{5, -3, -32768}),
            ref_processor(out0), ref_processor(out1), ref_processor(out2)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);
    in.require_output_checked(out2);

    in.feed(tc_event{{{100}, 5}, 123});
    REQUIRE(out0.check(tc_event{{{100}, 5}, 123}));
    in.feed(tc_event{{{101}, -3}, 123});
    REQUIRE(out1.check(tc_event{{{101}, -3}, 123}));
    in.feed(tc_event{{{102}, 0}, 124});
    in.feed(marker_event{{{103}, 0}});
    REQUIRE(out0.check(marker_event{{{103}, 0}}));
    REQUIRE(out1.check(marker_event{{{103}, 0}}));
    REQUIRE(out2.check(marker_event{{{103}, 0}}));
    in.feed_end();
    REQUIRE(out0.check_end());
    REQUIRE(out1.check_end());
    REQUIRE(out2.check_end());
}
