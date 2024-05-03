/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/prepend_append.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tcspc {

TEST_CASE("type constraints: prepend") {
    struct e0 {};
    struct e1 {};
    STATIC_CHECK(is_processor_v<decltype(prepend(e0{}, sink_events<e0, e1>())),
                                e0, e1>);
}

TEST_CASE("type constraints: append") {
    struct e0 {};
    struct e1 {};
    STATIC_CHECK(
        is_processor_v<decltype(append(e0{}, sink_events<e0, e1>())), e0, e1>);
}

TEST_CASE("introspect: prepend/append") {
    check_introspect_simple_processor(prepend<int>(42, null_sink()));
    check_introspect_simple_processor(append<int>(42, null_sink()));
}

TEST_CASE("prepend") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        prepend<int>(42, capture_output<type_list<int, double>>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<int, double>>(valcat, ctx, "out");

    in.handle(12.5);
    REQUIRE(out.check(emitted_as::always_rvalue, 42));
    REQUIRE(out.check(emitted_as::same_as_fed, 12.5));
    in.handle(25.0);
    REQUIRE(out.check(emitted_as::same_as_fed, 25.0));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("append") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        append<int>(42, capture_output<type_list<int, double>>(
                            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<int, double>>(valcat, ctx, "out");

    in.handle(12.5);
    REQUIRE(out.check(emitted_as::same_as_fed, 12.5));
    in.handle(25.0);
    REQUIRE(out.check(emitted_as::same_as_fed, 25.0));
    in.flush();
    REQUIRE(out.check(emitted_as::always_rvalue, 42));
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
