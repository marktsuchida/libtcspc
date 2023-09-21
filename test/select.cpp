/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/select.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;
using out_events = event_set<e0, e1>;

} // namespace

TEST_CASE("select", "[select]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0, e1>>(
        select<event_set<e0>>(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(e0{});
    REQUIRE(out.check(e0{}));
    in.feed(e1{});
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("select_not", "[select]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0, e1>>(
        select_not<event_set<e0>>(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(e0{});
    in.feed(e1{});
    REQUIRE(out.check(e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("select_none", "[select]") {
    auto ctx = std::make_shared<processor_context>();
    auto in =
        feed_input<event_set<e0, e1>>(select_none(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(e0{});
    in.feed(e1{});
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("select_all", "[select]") {
    auto ctx = std::make_shared<processor_context>();
    auto in =
        feed_input<event_set<e0, e1>>(select_all(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(e0{});
    REQUIRE(out.check(e0{}));
    in.feed(e1{});
    REQUIRE(out.check(e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
