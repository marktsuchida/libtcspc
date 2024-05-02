/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/match.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace tcspc {

namespace {

using some_event = time_tagged_test_event<0>;
using output_event = time_tagged_test_event<1>;
using misc_event = time_tagged_test_event<2>;
using out_events = type_list<marker_event<>, output_event, misc_event>;

} // namespace

TEST_CASE("type constraints: match") {
    using proc_type = decltype(match<some_event, output_event>(
        always_matcher(),
        sink_events<some_event, output_event, misc_event>()));
    STATIC_CHECK(is_processor_v<proc_type, some_event, misc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("type constraints: match_replace") {
    using proc_type = decltype(match_replace<some_event, output_event>(
        always_matcher(),
        sink_events<some_event, output_event, misc_event>()));
    STATIC_CHECK(is_processor_v<proc_type, some_event, misc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("introspect: match") {
    check_introspect_simple_processor(
        match_replace<int, long>(never_matcher(), null_sink()));
    check_introspect_simple_processor(
        match<int, long>(never_matcher(), null_sink()));
}

TEST_CASE("Match and replace") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         match_replace<marker_event<>, output_event>(
                             channel_matcher(arg::channel{0}),
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(marker_event<>{100, 0});
    REQUIRE(out.check(emitted_as::always_rvalue, output_event{100}));
    in.handle(marker_event<>{200, 1});
    REQUIRE(out.check(emitted_as::same_as_fed, marker_event<>{200, 1}));
    in.handle(misc_event{300});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{300}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("Match") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         match<marker_event<>, output_event>(
                             channel_matcher(arg::channel{0}),
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(marker_event<>{100, 0});
    REQUIRE(out.check(emitted_as::same_as_fed,
                      marker_event<>{100, 0})); // Preserved
    REQUIRE(out.check(emitted_as::always_rvalue, output_event{100}));
    in.handle(marker_event<>{200, 1});
    REQUIRE(out.check(emitted_as::same_as_fed, marker_event<>{200, 1}));
    in.handle(misc_event{300});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{300}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
