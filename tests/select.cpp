/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/select.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;
using out_events = type_list<e0, e1>;

} // namespace

TEST_CASE("type constraints: select") {
    STATIC_CHECK(
        is_processor_v<decltype(select<type_list<e1>>(sink_events<e1>())), e0,
                       e1>);
    STATIC_CHECK(
        is_processor_v<decltype(select_none(sink_events<>())), e0, e1>);
    STATIC_CHECK(
        is_processor_v<decltype(select_not<type_list<e1>>(sink_events<e0>())),
                       e0, e1>);
    STATIC_CHECK(
        is_processor_v<decltype(select_all(sink_events<e0, e1>())), e0, e1>);
}

TEST_CASE("introspect: select") {
    check_introspect_simple_processor(select<type_list<>>(null_sink()));
    check_introspect_simple_processor(select_none(null_sink()));
    check_introspect_simple_processor(select_not<type_list<>>(null_sink()));
    check_introspect_simple_processor(select_all(null_sink()));
}

TEST_CASE("select") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, select<type_list<e0>>(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(e0{});
    REQUIRE(out.check(emitted_as::same_as_fed, e0{}));
    in.handle(e1{});
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("select_not") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         select_not<type_list<e0>>(capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(e0{});
    in.handle(e1{});
    REQUIRE(out.check(emitted_as::same_as_fed, e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("select_none") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, select_none(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(e0{});
    in.handle(e1{});
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("select_all") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, select_all(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(e0{});
    REQUIRE(out.check(emitted_as::same_as_fed, e0{}));
    in.handle(e1{});
    REQUIRE(out.check(emitted_as::same_as_fed, e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
