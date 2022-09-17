/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/count.hpp"

#include "flimevt/event_set.hpp"
#include "flimevt/ref_processor.hpp"
#include "flimevt/test_utils.hpp"

#include <catch2/catch.hpp>

using namespace flimevt;

using input_event = timestamped_test_event<0>;
using output_event = timestamped_test_event<1>;
using reset_event = timestamped_test_event<2>;
using misc_event = timestamped_test_event<3>;

TEST_CASE("Count event", "[count_event]") {
    auto out = capture_output<
        event_set<input_event, output_event, reset_event, misc_event>>();

    SECTION("Threshold 0, limit 1") {
        SECTION("Emit before") {
            auto in =
                feed_input<event_set<input_event, reset_event, misc_event>>(
                    count_event<input_event, reset_event, output_event, false>(
                        0, 1, ref_processor(out)));
            in.require_output_checked(out);

            in.feed(input_event{42});
            REQUIRE(out.check(output_event{42}));
            REQUIRE(out.check(input_event{42}));
            in.feed(input_event{43});
            REQUIRE(out.check(output_event{43}));
            REQUIRE(out.check(input_event{43}));
            in.feed(reset_event{44});
            REQUIRE(out.check(reset_event{44}));
            in.feed(input_event{45});
            REQUIRE(out.check(output_event{45}));
            REQUIRE(out.check(input_event{45}));
            in.feed(misc_event{46});
            REQUIRE(out.check(misc_event{46}));
            in.feed_end();
            REQUIRE(out.check_end());
        }

        SECTION("Emit after") {
            auto in = feed_input<event_set<input_event>>(
                count_event<input_event, reset_event, output_event, true>(
                    0, 1, ref_processor(out)));
            in.require_output_checked(out);

            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            in.feed_end();
            REQUIRE(out.check_end());
        }
    }

    SECTION("Threshold 1, limit 1") {
        SECTION("Emit before") {
            auto in = feed_input<event_set<input_event>>(
                count_event<input_event, reset_event, output_event, false>(
                    1, 1, ref_processor(out)));
            in.require_output_checked(out);

            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            in.feed_end();
            REQUIRE(out.check_end());
        }

        SECTION("Emit after") {
            auto in = feed_input<event_set<input_event>>(
                count_event<input_event, reset_event, output_event, true>(
                    1, 1, ref_processor(out)));
            in.require_output_checked(out);

            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            REQUIRE(out.check(output_event{42}));
            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            REQUIRE(out.check(output_event{42}));
            in.feed_end();
            REQUIRE(out.check_end());
        }
    }

    SECTION("Threshold 1, limit 2") {
        SECTION("Emit before") {
            auto in = feed_input<event_set<input_event, reset_event>>(
                count_event<input_event, reset_event, output_event, false>(
                    1, 2, ref_processor(out)));
            in.require_output_checked(out);

            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            in.feed(input_event{43});
            REQUIRE(out.check(output_event{43}));
            REQUIRE(out.check(input_event{43}));
            in.feed(input_event{44});
            REQUIRE(out.check(input_event{44}));
            in.feed(reset_event{});
            REQUIRE(out.check(reset_event{}));
            in.feed(input_event{45});
            REQUIRE(out.check(input_event{45}));
            in.feed(input_event{46});
            REQUIRE(out.check(output_event{46}));
            REQUIRE(out.check(input_event{46}));
            in.feed_end();
            REQUIRE(out.check_end());
        }

        SECTION("Emit after") {
            auto in = feed_input<event_set<input_event, reset_event>>(
                count_event<input_event, reset_event, output_event, true>(
                    1, 2, ref_processor(out)));
            in.require_output_checked(out);

            in.feed(input_event{42});
            REQUIRE(out.check(input_event{42}));
            REQUIRE(out.check(output_event{42}));
            in.feed(input_event{43});
            REQUIRE(out.check(input_event{43}));
            in.feed(input_event{44});
            REQUIRE(out.check(input_event{44}));
            REQUIRE(out.check(output_event{44}));
            in.feed(reset_event{});
            REQUIRE(out.check(reset_event{}));
            in.feed(input_event{45});
            REQUIRE(out.check(input_event{45}));
            REQUIRE(out.check(output_event{45}));
            in.feed(input_event{46});
            REQUIRE(out.check(input_event{46}));
            in.feed_end();
            REQUIRE(out.check_end());
        }
    }
}