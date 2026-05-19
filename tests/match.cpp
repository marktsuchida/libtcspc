/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/match.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/processor.hpp"
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

struct wrong_return_matcher {
    auto operator()(some_event const & /*event*/) const -> int { return 0; }
};

struct mutable_only_matcher {
    auto operator()(some_event const & /*event*/) -> bool { return true; }
};

struct nonmovable_matcher {
    nonmovable_matcher() = default;
    nonmovable_matcher(nonmovable_matcher const &) = delete;
    auto operator=(nonmovable_matcher const &)
        -> nonmovable_matcher & = delete;
    nonmovable_matcher(nonmovable_matcher &&) = delete;
    auto operator=(nonmovable_matcher &&) -> nonmovable_matcher & = delete;
    ~nonmovable_matcher() = default;

    auto operator()(some_event const & /*event*/) const -> bool {
        return true;
    }
};

} // namespace

TEST_CASE("type constraints: match") {
    using proc_type = decltype(match<some_event, output_event>(
        always_matcher(), sink_only<some_event, output_event, misc_event>()));
    STATIC_CHECK(processor<proc_type, some_event, misc_event>);
    STATIC_CHECK_FALSE(handler_for<proc_type, int>);
}

TEST_CASE("matcher_for concept") {
    STATIC_CHECK(matcher_for<always_matcher, some_event>);
    STATIC_CHECK(matcher_for<never_matcher, some_event>);
    STATIC_CHECK(matcher_for<channel_matcher<>, marker_event<>>);

    STATIC_CHECK_FALSE(matcher_for<wrong_return_matcher, some_event>);
    STATIC_CHECK_FALSE(matcher_for<mutable_only_matcher, some_event>);
    STATIC_CHECK_FALSE(matcher_for<nonmovable_matcher, some_event>);
}

TEST_CASE("type constraints: match_and_consume") {
    using proc_type = decltype(match_and_consume<some_event, output_event>(
        always_matcher(), sink_only<some_event, output_event, misc_event>()));
    STATIC_CHECK(processor<proc_type, some_event, misc_event>);
    STATIC_CHECK_FALSE(handler_for<proc_type, int>);
}

TEST_CASE("introspect: match") {
    check_introspect_simple_processor(
        match_and_consume<int, long>(never_matcher(), sink_all()));
    check_introspect_simple_processor(
        match<int, long>(never_matcher(), sink_all()));
}

TEST_CASE("Match and consume") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         match_and_consume<marker_event<>, output_event>(
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
