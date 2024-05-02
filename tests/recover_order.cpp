/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/recover_order.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace tcspc {

namespace {

using e0 = time_tagged_test_event<0>;
using e1 = time_tagged_test_event<1>;

} // namespace

TEST_CASE("type constraints: recover_order") {
    using proc_type = decltype(recover_order<type_list<e0, e1>>(
        arg::time_window<i64>{100}, sink_events<e0, e1>()));
    STATIC_CHECK(is_processor_v<proc_type, e0, e1>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("introspect: recover_order") {
    check_introspect_simple_processor(
        recover_order<type_list<e0>>(arg::time_window<i64>{1}, null_sink()));
}

TEST_CASE("recover order") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         recover_order<type_list<e0>>(
                             arg::time_window<i64>{3},
                             capture_output<type_list<e0>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<e0>>(valcat, ctx, "out");

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("in-order events are delayed") {
        in.handle(e0{0});
        in.handle(e0{2});
        in.handle(e0{3});
        in.handle(e0{4});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{0}));
        in.handle(e0{5});
        in.handle(e0{6});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{2}));
        in.flush();
        REQUIRE(out.check(emitted_as::always_rvalue, e0{3}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{4}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{5}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{6}));
        REQUIRE(out.check_flushed());
    }

    SECTION("out-of-order events are sorted") {
        in.handle(e0{3});
        in.handle(e0{0});
        in.handle(e0{5});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{0}));
        in.handle(e0{2});
        in.handle(e0{7});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{2}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{3}));
        in.flush();
        REQUIRE(out.check(emitted_as::always_rvalue, e0{5}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{7}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("recover order, empty time window") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         recover_order<type_list<e0>>(
                             arg::time_window<i64>{0},
                             capture_output<type_list<e0>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<e0>>(valcat, ctx, "out");

    SECTION("in-order events are delayed") {
        in.handle(e0{0});
        in.handle(e0{0});
        in.handle(e0{2});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{0}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{0}));
        in.handle(e0{3});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{2}));
        in.handle(e0{4});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{3}));
        in.handle(e0{5});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{4}));
        in.handle(e0{6});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{5}));
        in.flush();
        REQUIRE(out.check(emitted_as::always_rvalue, e0{6}));
        REQUIRE(out.check_flushed());
    }

    SECTION("out-of-order event does not throw if recoverable") {
        in.handle(e0{42});
        in.handle(e0{41});
        in.handle(e0{42});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{41}));
        in.handle(e0{43});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{42}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{42}));
        in.handle(e0{42});
        in.handle(e0{43});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{42}));
        in.flush();
        REQUIRE(out.check(emitted_as::always_rvalue, e0{43}));
        REQUIRE(out.check(emitted_as::always_rvalue, e0{43}));
        REQUIRE(out.check_flushed());
    }

    SECTION("out-of-order event throws if too late") {
        in.handle(e0{42});
        in.handle(e0{43});
        REQUIRE(out.check(emitted_as::always_rvalue, e0{42}));
        REQUIRE_THROWS_AS(in.handle(e0{41}), data_validation_error);
    }
}

TEST_CASE("recover order, multiple event types") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         recover_order<type_list<e0, e1>>(
                             arg::time_window<i64>{3},
                             capture_output<type_list<e0, e1>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<e0, e1>>(valcat, ctx, "out");

    in.handle(e0{3});
    in.handle(e1{0});
    in.handle(e0{5});
    REQUIRE(out.check(emitted_as::always_rvalue, e1{0}));
    in.handle(e1{2});
    in.handle(e0{7});
    REQUIRE(out.check(emitted_as::always_rvalue, e1{2}));
    REQUIRE(out.check(emitted_as::always_rvalue, e0{3}));
    in.flush();
    REQUIRE(out.check(emitted_as::always_rvalue, e0{5}));
    REQUIRE(out.check(emitted_as::always_rvalue, e0{7}));
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
