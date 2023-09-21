/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/recover_order.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using event = timestamped_test_event<0>;

TEST_CASE("recover order", "[recover_order]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<event>>(recover_order<event>(
        3, capture_output<event_set<event>>(
               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<event>>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("in-order events are delayed") {
        in.feed(event{0});
        in.feed(event{2});
        in.feed(event{3});
        in.feed(event{4});
        REQUIRE(out.check(event{0}));
        in.feed(event{5});
        in.feed(event{6});
        REQUIRE(out.check(event{2}));
        in.flush();
        REQUIRE(out.check(event{3}));
        REQUIRE(out.check(event{4}));
        REQUIRE(out.check(event{5}));
        REQUIRE(out.check(event{6}));
        REQUIRE(out.check_flushed());
    }

    SECTION("out-of-order events are sorted") {
        in.feed(event{3});
        in.feed(event{0});
        in.feed(event{5});
        REQUIRE(out.check(event{0}));
        in.feed(event{2});
        in.feed(event{7});
        REQUIRE(out.check(event{2}));
        REQUIRE(out.check(event{3}));
        in.flush();
        REQUIRE(out.check(event{5}));
        REQUIRE(out.check(event{7}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("recover order, empty time window", "[recover_order]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<event>>(recover_order<event>(
        0, capture_output<event_set<event>>(
               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<event>>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("in-order events are delayed") {
        in.feed(event{0});
        in.feed(event{0});
        in.feed(event{2});
        REQUIRE(out.check(event{0}));
        REQUIRE(out.check(event{0}));
        in.feed(event{3});
        REQUIRE(out.check(event{2}));
        in.feed(event{4});
        REQUIRE(out.check(event{3}));
        in.feed(event{5});
        REQUIRE(out.check(event{4}));
        in.feed(event{6});
        REQUIRE(out.check(event{5}));
        in.flush();
        REQUIRE(out.check(event{6}));
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
