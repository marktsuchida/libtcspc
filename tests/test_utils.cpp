/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/test_utils.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <stdexcept>

namespace tcspc {

TEST_CASE("introspect test_utils", "[introspect]") {
    auto ctx = context::create();
    check_introspect_simple_sink(capture_output<type_list<>>(
        ctx->tracker<capture_output_access>("out0")));
    check_introspect_simple_sink(capture_output<type_list<int>>(
        ctx->tracker<capture_output_access>("out1")));
    check_introspect_simple_processor(
        feed_input(feed_as::const_lvalue, null_sink()));
    check_introspect_simple_sink(sink_events<type_list<>>());
}

namespace {

using e0 = empty_test_event<0>;
using e1 = time_tagged_test_event<1>;

} // namespace

TEST_CASE("Short-circuited with no events") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, capture_output<type_list<>>(
                               ctx->tracker<capture_output_access>("out")));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<>>(valcat, ctx, "out");

    SECTION("End successfully") {
        in.flush();
        CHECK(out.check_flushed());
    }

    SECTION("Unflushed end") { CHECK(out.check_not_flushed()); }
}

TEST_CASE("Short-circuited with event set") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, capture_output<type_list<e0, e1>>(
                               ctx->tracker<capture_output_access>("out")));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<e0, e1>>(valcat, ctx, "out");

    SECTION("End successfully") {
        in.handle(e0{});
        CHECK(out.check(e0{}));

        in.handle(e1{42});
        CHECK(out.check(emitted_as::same_as_fed, e1{42}));

        in.flush();
        CHECK(out.check_flushed());
    }

    SECTION("Unflushed end") { CHECK(out.check_not_flushed()); }

    SECTION("Forget to check output before feeding event") {
        in.handle(e0{});
        CHECK_THROWS_AS(in.handle(e0{}), std::logic_error);
    }

    SECTION("Forget to check output before flushing") {
        in.handle(e0{});
        CHECK_THROWS_AS(in.flush(), std::logic_error);
    }

    SECTION("Forget to check output before asserting successful end") {
        in.handle(e0{});
        CHECK_THROWS_AS(out.check_flushed(), std::logic_error);
    }

    SECTION("Forget to check output before asserting unflushed end") {
        in.handle(e0{});
        CHECK_THROWS_AS(out.check_not_flushed(), std::logic_error);
    }

    SECTION("Expect the wrong value category") {
        auto const wrong = valcat == feed_as::const_lvalue
                               ? emitted_as::always_rvalue
                               : emitted_as::always_lvalue;
        in.handle(e0{});
        CHECK_THROWS_AS(out.check(wrong, e0{}), std::logic_error);
    }

    SECTION("Expect the wrong event type") {
        in.handle(e0{});
        CHECK_THROWS_AS(out.check(e1{0}), std::logic_error);
    }

    SECTION("Expect the wrong event value") {
        in.handle(e1{42});
        CHECK_THROWS_AS(out.check(e1{0}), std::logic_error);
    }
}

TEST_CASE("sink_events") {
    static_assert(handles_flush_v<sink_events<type_list<>>>);

    static_assert(not handles_rvalue_event_v<sink_events<type_list<>>, int>);
    static_assert(handles_rvalue_event_v<sink_events<type_list<int>>, int>);
    static_assert(
        handles_rvalue_event_v<sink_events<type_list<int>, type_list<int>>,
                               int>);
    static_assert(
        handles_rvalue_event_v<sink_events<type_list<>, type_list<int>>, int>);

    static_assert(not handles_const_event_v<sink_events<type_list<>>, int>);
    static_assert(handles_const_event_v<sink_events<type_list<int>>, int>);
    static_assert(
        not handles_const_event_v<sink_events<type_list<int>, type_list<int>>,
                                  int>);
    static_assert(
        not handles_const_event_v<sink_events<type_list<>, type_list<int>>,
                                  int>);
}

} // namespace tcspc
