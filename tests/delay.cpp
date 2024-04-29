/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/delay.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <memory>

namespace tcspc {

TEST_CASE("introspect delay", "[introspect]") {
    check_introspect_simple_processor(delay(arg::delta<i64>{0}, null_sink()));
    check_introspect_simple_processor(zero_base_abstime(null_sink()));
}

namespace {

using e0 = time_tagged_test_event<0>;
using e1 = time_tagged_test_event<1>;
using out_events = type_list<e0, e1>;

} // namespace

TEST_CASE("Delay") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("Zero delay is noop") {
        auto in = feed_input(
            valcat, delay(arg::delta<i64>{0},
                          capture_output<out_events>(
                              ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.feed(e0{0});
        REQUIRE(out.check(e0{0}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Delay +1") {
        auto in = feed_input(
            valcat, delay(arg::delta<i64>{1},
                          capture_output<out_events>(
                              ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.feed(e0{0});
        REQUIRE(out.check(e0{1}));
        in.feed(e1{1});
        REQUIRE(out.check(e1{2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Delay -1") {
        auto in = feed_input(
            valcat, delay(arg::delta<i64>{-1},
                          capture_output<out_events>(
                              ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.feed(e0{0});
        REQUIRE(out.check(e0{-1}));
        in.feed(e1{1});
        REQUIRE(out.check(e1{0}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("zero-base abstime") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, zero_base_abstime(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("Positive") {
        in.feed(e0{123});
        REQUIRE(out.check(e0{0}));
        in.feed(e1{125});
        REQUIRE(out.check(e1{2}));
        in.feed(
            e0{std::numeric_limits<default_data_types::abstime_type>::min()});
        REQUIRE(out.check(
            e0{std::numeric_limits<default_data_types::abstime_type>::max() -
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
            e0{std::numeric_limits<default_data_types::abstime_type>::max()});
        REQUIRE(out.check(
            e0{std::numeric_limits<default_data_types::abstime_type>::min() +
               122}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
