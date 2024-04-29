/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/check.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <string>

namespace tcspc {

TEST_CASE("introspect check", "[introspect]") {
    check_introspect_simple_processor(check_monotonic(null_sink()));
    check_introspect_simple_processor(
        check_alternating<int, long>(null_sink()));
}

TEST_CASE("check monotonic") {
    using e0 = time_tagged_test_event<0>;
    using out_events = type_list<e0, warning_event>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, check_monotonic(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(e0{-10});
    REQUIRE(out.check(emitted_as::same_as_fed, e0{-10}));
    in.handle(warning_event{"test"});
    REQUIRE(out.check(warning_event{"test"}));
    in.handle(e0{-10});
    REQUIRE(out.check(emitted_as::same_as_fed, e0{-10}));
    in.handle(e0{-11});
    auto const out_event = out.pop<warning_event>();
    CHECK(out_event.message.find("monotonic") != std::string::npos);
    REQUIRE(out.check(emitted_as::same_as_fed, e0{-11}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("check alternating") {
    using e0 = time_tagged_test_event<0>;
    using e1 = time_tagged_test_event<1>;
    using e2 = time_tagged_test_event<2>;
    using out_events = type_list<e0, e1, e2, warning_event>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         check_alternating<e0, e1>(capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("correct") {
        in.handle(e0{42});
        REQUIRE(out.check(emitted_as::same_as_fed, e0{42}));
        in.handle(e1{43});
        REQUIRE(out.check(emitted_as::same_as_fed, e1{43}));
        in.handle(e0{44});
        REQUIRE(out.check(emitted_as::same_as_fed, e0{44}));
        in.handle(e1{45});
        REQUIRE(out.check(emitted_as::same_as_fed, e1{45}));
        in.handle(e0{46});
        REQUIRE(out.check(emitted_as::same_as_fed, e0{46}));
        in.handle(e2{47});
        REQUIRE(out.check(emitted_as::same_as_fed, e2{47}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("wrong event first") {
        in.handle(e1{42});
        auto const out_event = out.pop<warning_event>();
        CHECK(out_event.message.find("alternat") != std::string::npos);
        REQUIRE(out.check(emitted_as::same_as_fed, e1{42}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("consecutive Event0") {
        in.handle(e0{42});
        REQUIRE(out.check(emitted_as::same_as_fed, e0{42}));
        in.handle(e0{43});
        auto const out_event = out.pop<warning_event>();
        CHECK(out_event.message.find("alternat") != std::string::npos);
        REQUIRE(out.check(emitted_as::same_as_fed, e0{43}));
    }

    SECTION("consecutive Event1") {
        in.handle(e0{42});
        REQUIRE(out.check(emitted_as::same_as_fed, e0{42}));
        in.handle(e1{43});
        REQUIRE(out.check(emitted_as::same_as_fed, e1{43}));
        in.handle(e1{44});
        auto const out_event = out.pop<warning_event>();
        CHECK(out_event.message.find("alternat") != std::string::npos);
        REQUIRE(out.check(emitted_as::same_as_fed, e1{44}));
    }
}

} // namespace tcspc
