/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/generate_timings.hpp"

#include "flimevt/event_set.hpp"
#include "flimevt/ref_processor.hpp"
#include "flimevt/test_utils.hpp"

#include <catch2/catch.hpp>

using namespace flimevt;

using trigger_event = timestamped_test_event<0>;
using output_event = timestamped_test_event<1>;
using misc_event = timestamped_test_event<2>;

TEST_CASE("Generate null timing",
          "[generate_timings][null_timing_generator]") {
    auto out = capture_output<event_set<trigger_event, output_event>>();
    auto in =
        feed_input<event_set<trigger_event>>(generate_timings<trigger_event>(
            null_timing_generator<output_event>(), ref_processor(out)));
    in.require_output_checked(out);

    in.feed(trigger_event{42});
    REQUIRE(out.check(trigger_event{42}));
    in.feed(trigger_event{43});
    REQUIRE(out.check(trigger_event{43}));
    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("Generate one-shot timing",
          "[generate_timings][one_shot_timing_generator]") {
    macrotime delay = GENERATE(0, 1, 2);
    auto out =
        capture_output<event_set<trigger_event, output_event, misc_event>>();
    auto in = feed_input<event_set<trigger_event, misc_event>>(
        generate_timings<trigger_event>(
            one_shot_timing_generator<output_event>(delay),
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("No trigger, no output") {
        SECTION("No events") {}
        SECTION("Pass through others") {
            in.feed(misc_event{42});
            REQUIRE(out.check(misc_event{42}));
        }
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Delayed output") {
        in.feed(trigger_event{42});
        REQUIRE(out.check(trigger_event{42}));
        SECTION("Nothing more") {}
        SECTION("Output generated") {
            if (delay > 0) {
                in.feed(misc_event{42 + delay - 1});
                REQUIRE(out.check(misc_event{42 + delay - 1}));
            }
            in.feed(misc_event{42 + delay});
            REQUIRE(out.check(output_event{42 + delay}));
            REQUIRE(out.check(misc_event{42 + delay}));
        }
        SECTION("Output not generated when overlapping with next trigger") {
            in.feed(trigger_event{42 + delay});
            REQUIRE(out.check(trigger_event{42 + delay}));
            SECTION("Nothing more") {}
            SECTION("Retrigger produces output") {
                in.feed(misc_event{42 + delay + delay});
                REQUIRE(out.check(output_event{42 + delay + delay}));
                REQUIRE(out.check(misc_event{42 + delay + delay}));
            }
        }
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Generate linear timing",
          "[generate_timings][linear_timing_generator]") {
    macrotime delay = GENERATE(0, 1, 2);
    macrotime interval = GENERATE(1, 2);

    auto out =
        capture_output<event_set<trigger_event, output_event, misc_event>>();

    SECTION("Count of 0") {
        auto in = feed_input<event_set<trigger_event>>(
            generate_timings<trigger_event>(
                linear_timing_generator<output_event>(delay, interval, 0),
                ref_processor(out)));
        in.require_output_checked(out);

        in.feed(trigger_event{42});
        REQUIRE(out.check(trigger_event{42}));
        in.feed(trigger_event{43 + delay});
        REQUIRE(out.check(trigger_event{43 + delay}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Count of 1") {
        auto in = feed_input<event_set<trigger_event, misc_event>>(
            generate_timings<trigger_event>(
                linear_timing_generator<output_event>(delay, interval, 1),
                ref_processor(out)));
        in.require_output_checked(out);

        SECTION("Delayed output") {
            in.feed(trigger_event{42});
            REQUIRE(out.check(trigger_event{42}));
            SECTION("Nothing more") {}
            SECTION("Output generated") {
                if (delay > 0) {
                    in.feed(misc_event{42 + delay - 1});
                    REQUIRE(out.check(misc_event{42 + delay - 1}));
                }
                in.feed(misc_event{42 + delay});
                REQUIRE(out.check(output_event{42 + delay}));
                REQUIRE(out.check(misc_event{42 + delay}));
                SECTION("Nothing more") {}
                SECTION("No second output") {
                    in.feed(misc_event{42 + delay + interval + 1});
                    REQUIRE(out.check(misc_event{42 + delay + interval + 1}));
                }
            }
            in.feed_end();
            REQUIRE(out.check_end());
        }
    }

    SECTION("Count of 2") {
        auto in = feed_input<event_set<trigger_event, misc_event>>(
            generate_timings<trigger_event>(
                linear_timing_generator<output_event>(delay, interval, 2),
                ref_processor(out)));
        in.require_output_checked(out);

        in.feed(trigger_event{42});
        REQUIRE(out.check(trigger_event{42}));
        if (delay > 0) {
            in.feed(misc_event{42 + delay - 1});
            REQUIRE(out.check(misc_event{42 + delay - 1}));
        }
        in.feed(misc_event{42 + delay});
        REQUIRE(out.check(output_event{42 + delay}));
        REQUIRE(out.check(misc_event{42 + delay}));
        in.feed(misc_event{42 + delay + interval - 1});
        REQUIRE(out.check(misc_event{42 + delay + interval - 1}));
        in.feed(misc_event{42 + delay + interval});
        REQUIRE(out.check(output_event{42 + delay + interval}));
        REQUIRE(out.check(misc_event{42 + delay + interval}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}