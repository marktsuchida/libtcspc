/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/multiplex.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("multiplex", "[multiplex]") {
    using out_events = event_set<event_variant<event_set<e0, e1>>>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0, e1>>(
        multiplex<event_set<e0, e1>>(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(e0{});
    REQUIRE(out.check(event_variant<event_set<e0, e1>>(e0{})));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("demultiplex", "[demultiplex]") {
    using out_events = event_set<e0, e1>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<event_variant<event_set<e0, e1>>>>(
        demultiplex<event_set<e0, e1>>(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(event_variant<event_set<e0, e1>>(e1{}));
    REQUIRE(out.check(e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
