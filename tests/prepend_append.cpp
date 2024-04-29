/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/prepend_append.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tcspc {

TEST_CASE("introspect prepend/append", "[introspect]") {
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

    in.feed(12.5);
    REQUIRE(out.check(emitted_as::always_rvalue, 42));
    REQUIRE(out.check(emitted_as::same_as_fed, 12.5));
    in.feed(25.0);
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

    in.feed(12.5);
    REQUIRE(out.check(emitted_as::same_as_fed, 12.5));
    in.feed(25.0);
    REQUIRE(out.check(emitted_as::same_as_fed, 25.0));
    in.flush();
    REQUIRE(out.check(emitted_as::always_rvalue, 42));
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
