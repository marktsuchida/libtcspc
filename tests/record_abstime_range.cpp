/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/record_abstime_range.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <optional>

namespace tcspc {

namespace {

using tick_event = time_tagged_test_event<0>;
using misc_event = empty_test_event<1>;
using out_events = type_list<tick_event, misc_event>;

} // namespace

TEST_CASE("type constraints: record_abstime_range") {
    using proc_type = decltype(record_abstime_range(
        context::create()->tracker<record_abstime_range_access<i64>>("r"),
        sink_only<tick_event, misc_event>()));
    STATIC_CHECK(processor<proc_type, tick_event, misc_event>);
    STATIC_CHECK_FALSE(processor<proc_type, int>);
}

TEST_CASE("introspect: record_abstime_range") {
    auto ctx = context::create();
    check_introspect_simple_processor(record_abstime_range(
        ctx->tracker<record_abstime_range_access<i64>>("t"), sink_all()));
}

TEST_CASE("record abstime range") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, record_abstime_range(
                    ctx->tracker<record_abstime_range_access<i64>>("range"),
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");
    auto access = ctx->access<record_abstime_range_access<i64>>("range");

    CHECK(access.min() == std::nullopt);
    CHECK(access.max() == std::nullopt);

    in.handle(tick_event{10});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{10}));
    CHECK(access.min() == 10);
    CHECK(access.max() == 10);

    in.handle(tick_event{5});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{5}));
    CHECK(access.min() == 5);
    CHECK(access.max() == 10);

    in.handle(tick_event{20});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{20}));
    CHECK(access.min() == 5);
    CHECK(access.max() == 20);

    in.handle(misc_event{});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{}));
    CHECK(access.min() == 5);
    CHECK(access.max() == 20);

    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
