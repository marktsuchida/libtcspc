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
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace tcspc {

namespace {

using tick_event = time_tagged_test_event<0>;
using fire_event = time_tagged_test_event<1>;
using reset_event = time_tagged_test_event<2>;
using misc_event = time_tagged_test_event<3>;
using out_events = type_list<tick_event, fire_event, reset_event, misc_event>;

} // namespace

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
    auto ctx = context::create();

    SECTION("Threshold 0, limit 1") {
        SECTION("Emit before") {
            auto in =
                feed_input<type_list<tick_event, reset_event, misc_event>>(
                    count_up_to<tick_event, fire_event, reset_event, false>(
                        arg::threshold<u64>{0}, arg::limit<u64>{1},
                        arg::initial_count<u64>{0},
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(
                ctx->access<capture_output_access>("out"));

            in.feed(tick_event{42});
            REQUIRE(out.check(fire_event{42}));
            REQUIRE(out.check(tick_event{42}));
            in.feed(tick_event{43});
            REQUIRE(out.check(fire_event{43}));
            REQUIRE(out.check(tick_event{43}));
            in.feed(reset_event{44});
            REQUIRE(out.check(reset_event{44}));
            in.feed(tick_event{45});
            REQUIRE(out.check(fire_event{45}));
            REQUIRE(out.check(tick_event{45}));
            in.feed(misc_event{46});
            REQUIRE(out.check(misc_event{46}));
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("Emit after") {
            auto in = feed_input<type_list<tick_event>>(
                count_up_to<tick_event, fire_event, reset_event, true>(
                    arg::threshold<u64>{0}, arg::limit<u64>{1},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(
                ctx->access<capture_output_access>("out"));

            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            in.flush();
            REQUIRE(out.check_flushed());
        }
    }

    SECTION("Threshold 1, limit 1") {
        SECTION("Emit before") {
            auto in = feed_input<type_list<tick_event>>(
                count_up_to<tick_event, fire_event, reset_event, false>(
                    arg::threshold<u64>{1}, arg::limit<u64>{1},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(
                ctx->access<capture_output_access>("out"));

            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("Emit after") {
            auto in = feed_input<type_list<tick_event>>(
                count_up_to<tick_event, fire_event, reset_event, true>(
                    arg::threshold<u64>{1}, arg::limit<u64>{1},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(
                ctx->access<capture_output_access>("out"));

            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            REQUIRE(out.check(fire_event{42}));
            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            REQUIRE(out.check(fire_event{42}));
            in.flush();
            REQUIRE(out.check_flushed());
        }
    }

    SECTION("Threshold 1, limit 2") {
        SECTION("Emit before") {
            auto in = feed_input<type_list<tick_event, reset_event>>(
                count_up_to<tick_event, fire_event, reset_event, false>(
                    arg::threshold<u64>{1}, arg::limit<u64>{2},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(
                ctx->access<capture_output_access>("out"));

            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            in.feed(tick_event{43});
            REQUIRE(out.check(fire_event{43}));
            REQUIRE(out.check(tick_event{43}));
            in.feed(tick_event{44});
            REQUIRE(out.check(tick_event{44}));
            in.feed(reset_event{});
            REQUIRE(out.check(reset_event{}));
            in.feed(tick_event{45});
            REQUIRE(out.check(tick_event{45}));
            in.feed(tick_event{46});
            REQUIRE(out.check(fire_event{46}));
            REQUIRE(out.check(tick_event{46}));
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("Emit after") {
            auto in = feed_input<type_list<tick_event, reset_event>>(
                count_up_to<tick_event, fire_event, reset_event, true>(
                    arg::threshold<u64>{1}, arg::limit<u64>{2},
                    arg::initial_count<u64>{0},
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
            in.require_output_checked(ctx, "out");
            auto out = capture_output_checker<out_events>(
                ctx->access<capture_output_access>("out"));

            in.feed(tick_event{42});
            REQUIRE(out.check(tick_event{42}));
            REQUIRE(out.check(fire_event{42}));
            in.feed(tick_event{43});
            REQUIRE(out.check(tick_event{43}));
            in.feed(tick_event{44});
            REQUIRE(out.check(tick_event{44}));
            REQUIRE(out.check(fire_event{44}));
            in.feed(reset_event{});
            REQUIRE(out.check(reset_event{}));
            in.feed(tick_event{45});
            REQUIRE(out.check(tick_event{45}));
            REQUIRE(out.check(fire_event{45}));
            in.feed(tick_event{46});
            REQUIRE(out.check(tick_event{46}));
            in.flush();
            REQUIRE(out.check_flushed());
        }
    }
}

TEST_CASE("Count down to") {
    auto ctx = context::create();
    auto in = feed_input<type_list<tick_event, reset_event, misc_event>>(
        count_down_to<tick_event, fire_event, reset_event, false>(
            arg::threshold<u64>{1}, arg::limit<u64>{0},
            arg::initial_count<u64>{2},
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(tick_event{42});
    REQUIRE(out.check(tick_event{42}));
    in.feed(tick_event{43});
    REQUIRE(out.check(fire_event{43}));
    REQUIRE(out.check(tick_event{43}));
    in.feed(tick_event{44});
    REQUIRE(out.check(tick_event{44}));
    in.feed(tick_event{45});
    REQUIRE(out.check(fire_event{45}));
    REQUIRE(out.check(tick_event{45}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("event counter") {
    auto ctx = context::create();
    auto in = feed_input<type_list<tick_event, misc_event>>(
        count<tick_event>(ctx->tracker<count_access>("counter"),
                          capture_output<out_events>(
                              ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));
    auto counter = ctx->access<count_access>("counter");

    CHECK(counter.count() == 0);
    in.feed(tick_event{});
    REQUIRE(out.check(tick_event{}));
    CHECK(counter.count() == 1);
    in.feed(misc_event{});
    REQUIRE(out.check(misc_event{}));
    CHECK(counter.count() == 1);
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
