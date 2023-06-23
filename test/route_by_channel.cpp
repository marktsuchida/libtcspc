/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/route_by_channel.hpp"

#include "flimevt/ref_processor.hpp"
#include "flimevt/test_utils.hpp"
#include "flimevt/time_tagged_events.hpp"

#include <catch2/catch_all.hpp>

using namespace flimevt;

using tc_event = time_correlated_count_event;

TEST_CASE("Route photons", "[route_by_channel]") {
    auto out0 = capture_output<event_set<tc_event, marker_event>>();
    auto out1 = capture_output<event_set<tc_event, marker_event>>();
    auto out2 = capture_output<event_set<tc_event, marker_event>>();
    auto in = feed_input<event_set<tc_event, marker_event>>(
        route_by_channel<tc_event>({5, -3, -32768}, ref_processor(out0),
                                   ref_processor(out1), ref_processor(out2)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);
    in.require_output_checked(out2);

    in.feed(tc_event{{100}, 123, 5});
    REQUIRE(out0.check(tc_event{{100}, 123, 5}));
    in.feed(tc_event{{101}, 123, -3});
    REQUIRE(out1.check(tc_event{{101}, 123, -3}));
    in.feed(tc_event{{102}, 124, 0});
    in.feed(marker_event{{103}, 0});
    REQUIRE(out0.check(marker_event{{103}, 0}));
    REQUIRE(out1.check(marker_event{{103}, 0}));
    REQUIRE(out2.check(marker_event{{103}, 0}));
    in.feed_end();
    REQUIRE(out0.check_end());
    REQUIRE(out1.check_end());
    REQUIRE(out2.check_end());
}
