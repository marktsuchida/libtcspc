/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/match.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"

#include <catch2/catch_all.hpp>

using namespace tcspc;

using output_event = timestamped_test_event<0>;
using misc_event = timestamped_test_event<1>;

TEST_CASE("Match and replace", "[match_replace]") {
    auto out =
        capture_output<event_set<marker_event, output_event, misc_event>>();
    auto in = feed_input<event_set<marker_event, misc_event>>(
        match_replace<marker_event, output_event>(channel_matcher(0),
                                                  ref_processor(out)));
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

TEST_CASE("Match", "[match]") {
    auto out =
        capture_output<event_set<marker_event, output_event, misc_event>>();
    auto in = feed_input<event_set<marker_event, misc_event>>(
        match<marker_event, output_event>(static_channel_matcher<0>(),
                                          ref_processor(out)));
    in.require_output_checked(out);

    in.feed(marker_event{{100}, 0});
    REQUIRE(out.check(marker_event{{100}, 0})); // Preserved
    REQUIRE(out.check(output_event{100}));
    in.feed(marker_event{{200}, 1});
    REQUIRE(out.check(marker_event{{200}, 1}));
    in.feed(misc_event{300});
    REQUIRE(out.check(misc_event{300}));
    in.feed_end();
    REQUIRE(out.check_end());
}
