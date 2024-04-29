/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/stop.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("introspect stop", "[introspect]") {
    check_introspect_simple_processor(
        stop_with_error<type_list<>>("", null_sink()));
    check_introspect_simple_processor(stop<type_list<>>("", null_sink()));
}

TEST_CASE("stop with error") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        stop_with_error<type_list<e0>>(
            "myerror", capture_output<type_list<e1>>(
                           ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<e1>>(valcat, ctx, "out");

    in.handle(e1{});
    REQUIRE(out.check(emitted_as::same_as_fed, e1{}));
    REQUIRE_THROWS_WITH(
        in.handle(e0{}),
        Catch::Matchers::ContainsSubstring("myerror") &&
            Catch::Matchers::ContainsSubstring("empty_test_event<0>"));
    REQUIRE(out.check_not_flushed());
}

TEST_CASE("stop with no error") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        stop<type_list<e0>>("end of stream",
                            capture_output<type_list<e1>>(
                                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<e1>>(valcat, ctx, "out");

    in.handle(e1{});
    REQUIRE(out.check(emitted_as::same_as_fed, e1{}));
    REQUIRE_THROWS_AS(in.handle(e0{}), end_of_processing);
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
