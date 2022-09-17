/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/translate_marker.hpp"

#include "flimevt/ref_processor.hpp"
#include "flimevt/test_utils.hpp"
#include "flimevt/time_tagged_events.hpp"

#include <catch2/catch.hpp>

using namespace flimevt;

using output_event = timestamped_test_event<0>;
using misc_event = timestamped_test_event<1>;

TEST_CASE("Translate marker", "[translate_marker]") {
    auto out =
        capture_output<event_set<marker_event, output_event, misc_event>>();
    auto in = feed_input<event_set<marker_event, misc_event>>(
        translate_marker<marker_event, output_event>(0, ref_processor(out)));
    in.require_output_checked(out);

    in.feed(marker_event{{100}, 0});
    REQUIRE(out.check(output_event{100}));
    in.feed(marker_event{{200}, 1});
    REQUIRE(out.check(marker_event{{200}, 1}));
    in.feed(misc_event{300});
    REQUIRE(out.check(misc_event{300}));
    in.feed_end();
    REQUIRE(out.check_end());
}