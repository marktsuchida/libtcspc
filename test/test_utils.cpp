/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/test_utils.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/processor_context.hpp"

#include <exception>
#include <ostream>
#include <stdexcept>

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = empty_test_event<0>;
using e1 = timestamped_test_event<1>;

TEST_CASE("Short-circuited with no events", "[test_utils]") {
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

TEST_CASE("Short-circuited with event set", "[test_utils]") {
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
