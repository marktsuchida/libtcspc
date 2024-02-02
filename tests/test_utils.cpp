/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/test_utils.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/processor_context.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_all.hpp>

#include <memory>
#include <stdexcept>

namespace tcspc {

TEST_CASE("introspect test_utils", "[introspect]") {
    auto ctx = std::make_shared<processor_context>();
    check_introspect_simple_sink(capture_output<event_set<>>(
        ctx->tracker<capture_output_access>("out0")));
    check_introspect_simple_sink(capture_output<event_set<int>>(
        ctx->tracker<capture_output_access>("out1")));
    check_introspect_simple_source(feed_input<event_set<>>(null_sink()));
    check_introspect_simple_sink(event_set_sink<event_set<>>());
    check_introspect_simple_processor(
        check_event_set<event_set<>>(null_sink()));
}

using e0 = empty_test_event<0>;
using e1 = timestamped_test_event<1>;

TEST_CASE("Short-circuited with no events") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<>>(capture_output<event_set<>>(
        ctx->tracker<capture_output_access>("out")));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<>>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("End successfully") {
        in.flush();
        CHECK(out.check_flushed());
    }

    SECTION("Unflushed end") { CHECK(out.check_not_flushed()); }
}

TEST_CASE("Short-circuited with event set") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0, e1>>(capture_output<event_set<e0, e1>>(
        ctx->tracker<capture_output_access>("out")));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<e0, e1>>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(e0{});
    CHECK(out.check(e0{}));

    in.feed(e1{42});
    CHECK(out.check(e1{42}));

    SECTION("End successfully") {
        in.flush();
        CHECK(out.check_flushed());
    }

    SECTION("Unflushed end") { CHECK(out.check_not_flushed()); }

    SECTION("Forget to check output before feeding event") {
        in.feed(e0{});
        CHECK_THROWS_AS(in.feed(e0{}), std::logic_error);
    }

    SECTION("Forget to check output before flushing") {
        in.feed(e0{});
        CHECK_THROWS_AS(in.flush(), std::logic_error);
    }

    SECTION("Forget to check output before asserting successful end") {
        in.feed(e0{});
        CHECK_THROWS_AS(out.check_flushed(), std::logic_error);
    }

    SECTION("Forget to check output before asserting unflushed end") {
        in.feed(e0{});
        CHECK_THROWS_AS(out.check_not_flushed(), std::logic_error);
    }

    SECTION("Expect the wrong event") {
        in.feed(e1{42});
        CHECK_THROWS_AS(out.check(e1{0}), std::logic_error);
    }
}

} // namespace tcspc
