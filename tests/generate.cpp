/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/generate.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace tcspc {

namespace {

using trigger_event = time_tagged_test_event<0>;
using output_event = time_tagged_test_event<1>;
using misc_event = time_tagged_test_event<2>;
using out_events = type_list<trigger_event, output_event, misc_event>;

} // namespace

TEST_CASE("introspect generate", "[introspect]") {
    check_introspect_simple_processor(generate<trigger_event, output_event>(
        null_timing_generator(), null_sink()));
}

TEST_CASE("Generate null timing") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         generate<trigger_event, output_event>(
                             null_timing_generator(),
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(trigger_event{42});
    REQUIRE(out.check(trigger_event{42}));
    in.feed(trigger_event{43});
    REQUIRE(out.check(trigger_event{43}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("Generate one-shot timing",
          "[generate][one_shot_timing_generator]") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    default_data_types::abstime_type const delay = GENERATE(0, 1, 2);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         generate<trigger_event, output_event>(
                             one_shot_timing_generator(arg::delay{delay}),
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

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

TEST_CASE("Generate linear timing") {
    default_data_types::abstime_type const delay = GENERATE(0, 1, 2);
    default_data_types::abstime_type const interval = GENERATE(1, 2);

    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("Count of 0") {
        auto in = feed_input(
            valcat, generate<trigger_event, output_event>(
                        linear_timing_generator(arg::delay{delay},
                                                arg::interval{interval},
                                                arg::count<std::size_t>{0}),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(trigger_event{42});
        REQUIRE(out.check(trigger_event{42}));
        in.feed(trigger_event{43 + delay});
        REQUIRE(out.check(trigger_event{43 + delay}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Count of 1") {
        auto in = feed_input(
            valcat, generate<trigger_event, output_event>(
                        linear_timing_generator(arg::delay{delay},
                                                arg::interval{interval},
                                                arg::count<std::size_t>{1}),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

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
        auto in = feed_input(
            valcat, generate<trigger_event, output_event>(
                        linear_timing_generator(arg::delay{delay},
                                                arg::interval{interval},
                                                arg::count<std::size_t>{2}),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

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

TEST_CASE("dynamic one-shot timing generator",
          "[dynamic_one_shot_timing_generator]") {
    auto tg = dynamic_one_shot_timing_generator();
    CHECK_FALSE(tg.peek().has_value());
    struct trig_evt {
        std::int64_t abstime;
        std::int64_t delay;
    };
    tg.trigger(trig_evt{42, 3});
    CHECK(tg.peek().has_value());
    auto const t0 =
        tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(t0 == 45);
    tg.pop();
    CHECK_FALSE(tg.peek().has_value());
}

TEST_CASE("dynamic linear timing generator",
          "[dynamic_linear_timing_generator]") {
    auto tg = dynamic_linear_timing_generator();
    CHECK_FALSE(tg.peek().has_value());
    struct trig_evt {
        std::int64_t abstime;
        std::int64_t delay;
        std::int64_t interval;
        std::size_t count;
    };
    tg.trigger(trig_evt{42, 3, 5, 2});
    CHECK(tg.peek().has_value());
    auto const t0 =
        tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(t0 == 45);
    tg.pop();
    CHECK(tg.peek().has_value());
    auto const t1 =
        tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(t1 == 50);
    tg.pop();
    CHECK_FALSE(tg.peek().has_value());
}

} // namespace tcspc
