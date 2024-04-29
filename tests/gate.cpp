/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/gate.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace tcspc {

namespace {

using open_event = empty_test_event<0>;
using close_event = empty_test_event<1>;
using gated_event = empty_test_event<2>;
using misc_event = empty_test_event<3>;
using out_events = type_list<open_event, close_event, gated_event, misc_event>;

} // namespace

TEST_CASE("introspect gate", "[introspect]") {
    check_introspect_simple_processor(
        gate<gated_event, open_event, close_event>(arg::initially_open{false},
                                                   null_sink()));
}

TEST_CASE("Gate events") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    bool const initially_open = GENERATE(false, true);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         gate<type_list<gated_event>, open_event, close_event>(
                             arg::initially_open{initially_open},
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("Initial state") {
        in.handle(gated_event{});
        if (initially_open)
            REQUIRE(out.check(emitted_as::same_as_fed, gated_event{}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Pass through unrelated events") {
        in.handle(misc_event{});
        REQUIRE(out.check(emitted_as::same_as_fed, misc_event{}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Pass through open/close") {
        in.handle(open_event{});
        REQUIRE(out.check(open_event{}));
        in.handle(close_event{});
        REQUIRE(out.check(close_event{}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Gate closed") {
        in.handle(close_event{});
        REQUIRE(out.check(close_event{}));
        in.handle(gated_event{});
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Gate open") {
        in.handle(open_event{});
        REQUIRE(out.check(open_event{}));
        in.handle(gated_event{});
        REQUIRE(out.check(emitted_as::same_as_fed, gated_event{}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
