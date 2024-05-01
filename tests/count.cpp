/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/count.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace tcspc {

namespace {

using tick_event = time_tagged_test_event<0>;
using fire_event = time_tagged_test_event<1>;
using reset_event = time_tagged_test_event<2>;
using misc_event = time_tagged_test_event<3>;
using out_events = type_list<tick_event, fire_event, reset_event, misc_event>;

} // namespace

TEST_CASE("count_up_to event type constraints") {
    using proc_noreset =
        decltype(count_up_to<tick_event, fire_event, reset_event, false>(
            arg::threshold<u64>{100}, arg::limit<u64>{100},
            arg::initial_count<u64>{0},
            sink_events<tick_event, fire_event, misc_event>()));
    using proc_reset =
        decltype(count_up_to<tick_event, fire_event, reset_event, false>(
            arg::threshold<u64>{100}, arg::limit<u64>{100},
            arg::initial_count<u64>{0},
            sink_events<tick_event, fire_event, reset_event, misc_event>()));
    STATIC_CHECK(is_processor_v<proc_noreset, tick_event, misc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_noreset, reset_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_noreset, int>);
    STATIC_CHECK(
        is_processor_v<proc_reset, tick_event, reset_event, misc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_reset, int>);
}

TEST_CASE("count event type constraints") {
    using proc_type = decltype(count<tick_event>(
        context::create()->tracker<count_access>("c"),
        sink_events<tick_event, misc_event>()));
    STATIC_CHECK(is_processor_v<proc_type, tick_event, misc_event>);
    STATIC_CHECK_FALSE(is_processor_v<proc_type, int>);
}

TEST_CASE("introspect count", "[introspect]") {
    check_introspect_simple_processor(
        count_up_to<tick_event, fire_event, reset_event, false>(
            arg::threshold<u64>{1}, arg::limit<u64>{2},
            arg::initial_count<u64>{0}, null_sink()));
    check_introspect_simple_processor(
        count_down_to<tick_event, fire_event, reset_event, false>(
            arg::threshold<u64>{1}, arg::limit<u64>{0},
            arg::initial_count<u64>{2}, null_sink()));
    auto ctx = context::create();
    check_introspect_simple_processor(
        count<tick_event>(ctx->tracker<count_access>("t"), null_sink()));
}

TEST_CASE("Count up to") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("Threshold 0, limit 1") {
        SECTION("Emit before") {
            auto in = feed_input(
                valcat,
                count_up_to<tick_event, fire_event, reset_event, false>(
                    arg::threshold<u64>{0}, arg::limit<u64>{1},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(valcat, ctx, "out");

            in.handle(tick_event{42});
            REQUIRE(out.check(fire_event{42}));
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            in.handle(tick_event{43});
            REQUIRE(out.check(fire_event{43}));
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{43}));
            in.handle(reset_event{44});
            REQUIRE(out.check(emitted_as::same_as_fed, reset_event{44}));
            in.handle(tick_event{45});
            REQUIRE(out.check(fire_event{45}));
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{45}));
            in.handle(misc_event{46});
            REQUIRE(out.check(emitted_as::same_as_fed, misc_event{46}));
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("Emit after") {
            auto in = feed_input(
                valcat, count_up_to<tick_event, fire_event, reset_event, true>(
                            arg::threshold<u64>{0}, arg::limit<u64>{1},
                            arg::initial_count<u64>{0},
                            capture_output<out_events>(
                                ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(valcat, ctx, "out");

            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            in.flush();
            REQUIRE(out.check_flushed());
        }
    }

    SECTION("Threshold 1, limit 1") {
        SECTION("Emit before") {
            auto in = feed_input(
                valcat,
                count_up_to<tick_event, fire_event, reset_event, false>(
                    arg::threshold<u64>{1}, arg::limit<u64>{1},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(valcat, ctx, "out");

            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("Emit after") {
            auto in = feed_input(
                valcat, count_up_to<tick_event, fire_event, reset_event, true>(
                            arg::threshold<u64>{1}, arg::limit<u64>{1},
                            arg::initial_count<u64>{0},
                            capture_output<out_events>(
                                ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(valcat, ctx, "out");

            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            REQUIRE(out.check(fire_event{42}));
            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            REQUIRE(out.check(fire_event{42}));
            in.flush();
            REQUIRE(out.check_flushed());
        }
    }

    SECTION("Threshold 1, limit 2") {
        SECTION("Emit before") {
            auto in = feed_input(
                valcat,
                count_up_to<tick_event, fire_event, reset_event, false>(
                    arg::threshold<u64>{1}, arg::limit<u64>{2},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(valcat, ctx, "out");

            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            in.handle(tick_event{43});
            REQUIRE(out.check(fire_event{43}));
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{43}));
            in.handle(tick_event{44});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{44}));
            in.handle(reset_event{});
            REQUIRE(out.check(emitted_as::same_as_fed, reset_event{}));
            in.handle(tick_event{45});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{45}));
            in.handle(tick_event{46});
            REQUIRE(out.check(fire_event{46}));
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{46}));
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("Emit after") {
            auto in = feed_input(
                valcat, count_up_to<tick_event, fire_event, reset_event, true>(
                            arg::threshold<u64>{1}, arg::limit<u64>{2},
                            arg::initial_count<u64>{0},
                            capture_output<out_events>(
                                ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(valcat, ctx, "out");

            in.handle(tick_event{42});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
            REQUIRE(out.check(fire_event{42}));
            in.handle(tick_event{43});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{43}));
            in.handle(tick_event{44});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{44}));
            REQUIRE(out.check(fire_event{44}));
            in.handle(reset_event{});
            REQUIRE(out.check(emitted_as::same_as_fed, reset_event{}));
            in.handle(tick_event{45});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{45}));
            REQUIRE(out.check(fire_event{45}));
            in.handle(tick_event{46});
            REQUIRE(out.check(emitted_as::same_as_fed, tick_event{46}));
            in.flush();
            REQUIRE(out.check_flushed());
        }
    }
}

TEST_CASE("Count down to") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, count_down_to<tick_event, fire_event, reset_event, false>(
                    arg::threshold<u64>{1}, arg::limit<u64>{0},
                    arg::initial_count<u64>{2},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(tick_event{42});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{42}));
    in.handle(tick_event{43});
    REQUIRE(out.check(fire_event{43}));
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{43}));
    in.handle(tick_event{44});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{44}));
    in.handle(tick_event{45});
    REQUIRE(out.check(fire_event{45}));
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{45}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("event counter") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        count<tick_event>(ctx->tracker<count_access>("counter"),
                          capture_output<out_events>(
                              ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");
    auto counter = ctx->access<count_access>("counter");

    CHECK(counter.count() == 0);
    in.handle(tick_event{});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{}));
    CHECK(counter.count() == 1);
    in.handle(misc_event{});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{}));
    CHECK(counter.count() == 1);
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
