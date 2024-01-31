/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/shared_processor.hpp"

#include "libtcspc/test_utils.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("introspect shared_processor", "[introspect]") {
    check_introspect_simple_processor(
        shared_processor(move_to_shared(null_sink())));
}

TEST_CASE("move to shared") {
    std::shared_ptr<int> const spi = move_to_shared(123);
    std::string s = "hello";
    // auto const sps = move_to_shared(s); // Fails to compile with lvalue.
    auto const sps = move_to_shared(std::move(s));
}

TEST_CASE("shared_processor") {
    using e = empty_test_event<0>;
    auto ctx = std::make_shared<processor_context>();
    auto shptr = move_to_shared(capture_output<event_set<e>>(
        ctx->tracker<capture_output_access>("out1")));

    auto in = feed_input<event_set<e>>(shared_processor(shptr));

    SECTION("Events are forwarded") {
        auto out1 = capture_output_checker<event_set<e>>(
            ctx->accessor<capture_output_access>("out1"));
        in.require_output_checked(ctx, "out1");
        in.feed(e{});
        REQUIRE(out1.check(e{}));
        in.flush();
        REQUIRE(out1.check_flushed());
    }

    SECTION("Downstream can be replaced via the shared_ptr") {
        *shptr = capture_output<event_set<e>>(
            ctx->tracker<capture_output_access>("out2"));
        in.require_output_checked(ctx, "out2");
        auto out2 = capture_output_checker<event_set<e>>(
            ctx->accessor<capture_output_access>("out2"));
        in.flush();
        REQUIRE(out2.check_flushed());
    }
}

} // namespace tcspc
