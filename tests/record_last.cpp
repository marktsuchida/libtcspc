/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/record_last.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/histogram_events.hpp"
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

TEST_CASE("type constraints: record_last") {
    using proc_type = decltype(record_last<tick_event>(
        context::create()->tracker<record_last_access<tick_event>>("r"),
        sink_only<tick_event, misc_event>()));
    STATIC_CHECK(processor<proc_type, tick_event, misc_event>);
    STATIC_CHECK_FALSE(processor<proc_type, int>);
}

TEST_CASE("introspect: record_last") {
    auto ctx = context::create();
    check_introspect_simple_processor(record_last<tick_event>(
        ctx->tracker<record_last_access<tick_event>>("t"), sink_all()));
}

TEST_CASE("record last") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, record_last<tick_event>(
                    ctx->tracker<record_last_access<tick_event>>("last"),
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");
    auto access = ctx->access<record_last_access<tick_event>>("last");

    CHECK(access.get() == std::nullopt);

    in.handle(tick_event{10});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{10}));
    CHECK(access.get() == tick_event{10});

    in.handle(tick_event{20});
    REQUIRE(out.check(emitted_as::same_as_fed, tick_event{20}));
    CHECK(access.get() == tick_event{20});

    // Events of a different type pass through and do not change the result.
    in.handle(misc_event{});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{}));
    CHECK(access.get() == tick_event{20});

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("record last retains bucket data after flush") {
    using hist_event = histogram_event<>;
    using hist_out_events = type_list<hist_event>;

    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, record_last<hist_event>(
                    ctx->tracker<record_last_access<hist_event>>("last"),
                    capture_output<hist_out_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hist_out_events>(valcat, ctx, "out");
    auto access = ctx->access<record_last_access<hist_event>>("last");

    in.handle(hist_event{test_bucket<u16>({1, 2, 3})});
    REQUIRE(out.check(emitted_as::same_as_fed,
                      hist_event{test_bucket<u16>({1, 2, 3})}));

    in.flush();
    REQUIRE(out.check_flushed());

    // The retained copy is independent of the (moved-through) original and its
    // bucket data is intact.
    CHECK(access.get() == hist_event{test_bucket<u16>({1, 2, 3})});
}

} // namespace tcspc
