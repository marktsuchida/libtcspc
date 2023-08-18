/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/generate.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using trigger_event = timestamped_test_event<0>;
using output_event = timestamped_test_event<1>;
using misc_event = timestamped_test_event<2>;

TEST_CASE("Generate null timing", "[generate][null_timing_generator]") {
    auto out = capture_output<event_set<trigger_event, output_event>>();
    auto in = feed_input<event_set<trigger_event>>(generate<trigger_event>(
        null_timing_generator<output_event>(), ref_processor(out)));
    in.require_output_checked(out);

    in.feed(trigger_event{42});
    REQUIRE(out.check(trigger_event{42}));
    in.feed(trigger_event{43});
    REQUIRE(out.check(trigger_event{43}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("Generate one-shot timing",
          "[generate][one_shot_timing_generator]") {
    default_data_traits::abstime_type const delay = GENERATE(0, 1, 2);
    auto out =
        capture_output<event_set<trigger_event, output_event, misc_event>>();
    auto in = feed_input<event_set<trigger_event, misc_event>>(
        generate<trigger_event>(one_shot_timing_generator<output_event>(delay),
                                ref_processor(out)));
    in.require_output_checked(out);

    SECTION("No trigger, no output") {
        SECTION("No events") {}
        SECTION("Pass through others") {
            in.feed(misc_event{42});
            REQUIRE(out.check(misc_event{42}));
        }
        in.flush();
        REQUIRE(out.check_flushed());
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
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Generate linear timing", "[generate][linear_timing_generator]") {
    default_data_traits::abstime_type const delay = GENERATE(0, 1, 2);
    default_data_traits::abstime_type const interval = GENERATE(1, 2);

    auto out =
        capture_output<event_set<trigger_event, output_event, misc_event>>();

    SECTION("Count of 0") {
        auto in = feed_input<event_set<trigger_event>>(generate<trigger_event>(
            linear_timing_generator<output_event>(delay, interval, 0),
            ref_processor(out)));
        in.require_output_checked(out);

        in.feed(trigger_event{42});
        REQUIRE(out.check(trigger_event{42}));
        in.feed(trigger_event{43 + delay});
        REQUIRE(out.check(trigger_event{43 + delay}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Count of 1") {
        auto in = feed_input<event_set<trigger_event, misc_event>>(
            generate<trigger_event>(
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
            in.flush();
            REQUIRE(out.check_flushed());
        }
    }

    SECTION("Count of 2") {
        auto in = feed_input<event_set<trigger_event, misc_event>>(
            generate<trigger_event>(
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
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

namespace internal {

TEST_CASE("uniformly distributed double", "[dither]") {
    CHECK(uniform_double_0_1(0) == 0.0);
    CHECK(uniform_double_0_1(std::uint64_t(-1)) < 1.0);
    CHECK_THAT(uniform_double_0_1(std::uint64_t(-1)),
               Catch::Matchers::WithinAbs(1.0, 1e-15));
}

TEST_CASE("uniformly distributed double, for std::minstd_rand", "[dither]") {
    CHECK(uniform_double_0_1_minstd(0) == 0.0);
    CHECK(uniform_double_0_1_minstd(1'073'741'824) == 0.5);
    CHECK(uniform_double_0_1_minstd(2'147'483'647) < 1.0);
    CHECK_THAT(uniform_double_0_1_minstd(2'147'483'647),
               Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("dither given random double in 1.0 to 2.0", "[dither]") {
    CHECK(dither<std::int64_t>(0.0, 0.0) == 0);
    CHECK(dither<std::int64_t>(0.1, 0.0) == 0);
    CHECK(dither<std::int64_t>(0.999, 0.0) == 0);
    CHECK(dither<std::int64_t>(1.0, 0.0) == 1);

    CHECK(dither<std::int64_t>(0.0, 0.9999) == 0);
    CHECK(dither<std::int64_t>(0.1, 0.9999) == 1);
    CHECK(dither<std::int64_t>(0.999, 0.9999) == 1);
    CHECK(dither<std::int64_t>(1.0, 0.9999) == 1);
}

TEST_CASE("dithering quantizer", "[dither]") {
    dithering_quantizer<std::int64_t> dq;
    CHECK(dq(-1.0) == -1);
    CHECK(dq(0.0) == 0);
    CHECK(dq(1.0) == 1);
    CHECK(dq(2.0) == 2);
    auto q = dq(1.5);
    CHECK(q >= 1);
    CHECK(q <= 2);
}

} // namespace internal

TEST_CASE("dithered linear timing generator",
          "[dithered_linear_timing_generator]") {
    // Test that generated timings always fulfill criteria, but also that all
    // possible values are seen within a reasonable number of repetitions.
    // (Note that this test should be reproducible because of our use of a
    // standardized PRNG with the same seed.)
    bool seen_t0_42 = false;
    bool seen_t0_43 = false;
    bool seen_t1_52 = false;
    bool seen_t1_53 = false;
    bool seen_t01_10 = false;
    bool seen_t01_11 = false;
    auto tg = dithered_linear_timing_generator<output_event>(0.5, 10.25, 2);
    for (std::size_t i = 0; i < 5; ++i) { // 5 is the sharp minimum to pass.
        CHECK_FALSE(tg.peek().has_value());
        tg.trigger(trigger_event{42});
        CHECK(tg.peek().has_value());
        auto const t0 =
            tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
        auto const e0 = tg.pop();
        CHECK(t0 == e0.abstime);
        CHECK(t0 >= 42);
        CHECK(t0 <= 43);
        seen_t0_42 = seen_t0_42 || t0 == 42;
        seen_t0_43 = seen_t0_43 || t0 == 43;
        CHECK(tg.peek().has_value());
        auto const t1 =
            tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
        auto const e1 = tg.pop();
        CHECK(t1 == e1.abstime);
        CHECK(t1 >= 52);
        CHECK(t1 <= 53);
        seen_t1_52 = seen_t1_52 || t1 == 52;
        seen_t1_53 = seen_t1_53 || t1 == 53;
        CHECK(t1 - t0 >= 10);
        CHECK(t1 - t0 <= 11);
        seen_t01_10 = seen_t01_10 || t1 - t0 == 10;
        seen_t01_11 = seen_t01_11 || t1 - t0 == 11;
    }
    CHECK(seen_t0_42);
    CHECK(seen_t0_43);
    CHECK(seen_t1_52);
    CHECK(seen_t1_53);
    CHECK(seen_t01_10);
    CHECK(seen_t01_11);
}

} // namespace tcspc
