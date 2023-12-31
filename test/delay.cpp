/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/delay.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;
using out_events = event_set<e0, e1>;

} // namespace

TEST_CASE("Delay", "[delay]") {
    auto ctx = std::make_shared<processor_context>();

    SECTION("Zero delay is noop") {
        auto in = feed_input<event_set<e0>>(
            delay(0, capture_output<out_events>(
                         ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(e0{0});
        REQUIRE(out.check(e0{0}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Delay +1") {
        auto in = feed_input<event_set<e0, e1>>(
            delay(1, capture_output<out_events>(
                         ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(e0{0});
        REQUIRE(out.check(e0{1}));
        in.feed(e1{1});
        REQUIRE(out.check(e1{2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Delay -1") {
        auto in = feed_input<event_set<e0, e1>>(
            delay(-1, capture_output<out_events>(
                          ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(e0{0});
        REQUIRE(out.check(e0{-1}));
        in.feed(e1{1});
        REQUIRE(out.check(e1{0}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("zero-base abstime", "[zero_base_abstime]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0, e1>>(
        zero_base_abstime(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("Positive") {
        in.feed(e0{123});
        REQUIRE(out.check(e0{0}));
        in.feed(e1{125});
        REQUIRE(out.check(e1{2}));
        in.feed(
            e0{std::numeric_limits<default_data_traits::abstime_type>::min()});
        REQUIRE(out.check(
            e0{std::numeric_limits<default_data_traits::abstime_type>::max() -
               122}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Negative") {
        in.feed(e0{-123});
        REQUIRE(out.check(e0{0}));
        in.feed(e1{-121});
        REQUIRE(out.check(e1{2}));
        in.feed(
            e0{std::numeric_limits<default_data_traits::abstime_type>::max()});
        REQUIRE(out.check(
            e0{std::numeric_limits<default_data_traits::abstime_type>::min() +
               122}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
