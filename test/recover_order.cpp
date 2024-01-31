/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/recover_order.hpp"

#include "libtcspc/test_utils.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;

} // namespace

TEST_CASE("introspect recover_order", "[introspect]") {
    check_introspect_simple_processor(
        recover_order<event_set<e0>>(1, null_sink()));
}

TEST_CASE("recover order") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0>>(recover_order<event_set<e0>>(
        3, capture_output<event_set<e0>>(
               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<e0>>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("in-order events are delayed") {
        in.feed(e0{0});
        in.feed(e0{2});
        in.feed(e0{3});
        in.feed(e0{4});
        REQUIRE(out.check(e0{0}));
        in.feed(e0{5});
        in.feed(e0{6});
        REQUIRE(out.check(e0{2}));
        in.flush();
        REQUIRE(out.check(e0{3}));
        REQUIRE(out.check(e0{4}));
        REQUIRE(out.check(e0{5}));
        REQUIRE(out.check(e0{6}));
        REQUIRE(out.check_flushed());
    }

    SECTION("out-of-order events are sorted") {
        in.feed(e0{3});
        in.feed(e0{0});
        in.feed(e0{5});
        REQUIRE(out.check(e0{0}));
        in.feed(e0{2});
        in.feed(e0{7});
        REQUIRE(out.check(e0{2}));
        REQUIRE(out.check(e0{3}));
        in.flush();
        REQUIRE(out.check(e0{5}));
        REQUIRE(out.check(e0{7}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("recover order, empty time window") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0>>(recover_order<event_set<e0>>(
        0, capture_output<event_set<e0>>(
               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<e0>>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("in-order events are delayed") {
        in.feed(e0{0});
        in.feed(e0{0});
        in.feed(e0{2});
        REQUIRE(out.check(e0{0}));
        REQUIRE(out.check(e0{0}));
        in.feed(e0{3});
        REQUIRE(out.check(e0{2}));
        in.feed(e0{4});
        REQUIRE(out.check(e0{3}));
        in.feed(e0{5});
        REQUIRE(out.check(e0{4}));
        in.feed(e0{6});
        REQUIRE(out.check(e0{5}));
        in.flush();
        REQUIRE(out.check(e0{6}));
        REQUIRE(out.check_flushed());
    }

    SECTION("out-of-order event does not throw if recoverable") {
        in.feed(e0{42});
        in.feed(e0{41});
        in.feed(e0{42});
        REQUIRE(out.check(e0{41}));
        in.feed(e0{43});
        REQUIRE(out.check(e0{42}));
        REQUIRE(out.check(e0{42}));
        in.feed(e0{42});
        in.feed(e0{43});
        REQUIRE(out.check(e0{42}));
        in.flush();
        REQUIRE(out.check(e0{43}));
        REQUIRE(out.check(e0{43}));
        REQUIRE(out.check_flushed());
    }

    SECTION("out-of-order event throws if too late") {
        in.feed(e0{42});
        in.feed(e0{43});
        REQUIRE(out.check(e0{42}));
        REQUIRE_THROWS_AS(in.feed(e0{41}), std::runtime_error);
    }
}

TEST_CASE("recover order, multiple event types") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0, e1>>(recover_order<event_set<e0, e1>>(
        3, capture_output<event_set<e0, e1>>(
               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<e0, e1>>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(e0{3});
    in.feed(e1{0});
    in.feed(e0{5});
    REQUIRE(out.check(e1{0}));
    in.feed(e1{2});
    in.feed(e0{7});
    REQUIRE(out.check(e1{2}));
    REQUIRE(out.check(e0{3}));
    in.flush();
    REQUIRE(out.check(e0{5}));
    REQUIRE(out.check(e0{7}));
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
