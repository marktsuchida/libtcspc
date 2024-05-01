/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/time_correlate.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstdint>
#include <memory>

namespace tcspc {

TEST_CASE("time_correlate_at_start event type constraints") {
    using proc_type = decltype(time_correlate_at_start(
        sink_events<time_correlated_detection_event<>, int>()));
    STATIC_CHECK(
        is_processor_v<proc_type, std::array<detection_event<>, 2>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("time_correlate_at_midpoint event type constraints") {
    using proc_type = decltype(time_correlate_at_midpoint(
        sink_events<time_correlated_detection_event<>, int>()));
    STATIC_CHECK(
        is_processor_v<proc_type, std::array<detection_event<>, 2>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("time_correlate_at_fraction event type constraints") {
    using proc_type = decltype(time_correlate_at_fraction(
        arg::fraction{0.333},
        sink_events<time_correlated_detection_event<>, int>()));
    STATIC_CHECK(
        is_processor_v<proc_type, std::array<detection_event<>, 2>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("negate_difftime event type constraints") {
    using proc_type = decltype(negate_difftime(
        sink_events<time_correlated_detection_event<>, int>()));
    STATIC_CHECK(
        is_processor_v<proc_type, time_correlated_detection_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("remove_time_correlation event type constraints") {
    using proc_type = decltype(remove_time_correlation(
        sink_events<detection_event<>, int>()));
    STATIC_CHECK(
        is_processor_v<proc_type, time_correlated_detection_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("introspect time_correlate", "[introspect]") {
    check_introspect_simple_processor(time_correlate_at_start(null_sink()));
    check_introspect_simple_processor(time_correlate_at_stop(null_sink()));
    check_introspect_simple_processor(time_correlate_at_midpoint(null_sink()));
    check_introspect_simple_processor(
        time_correlate_at_fraction(arg::fraction{0.3}, null_sink()));
    check_introspect_simple_processor(negate_difftime(null_sink()));
    check_introspect_simple_processor(remove_time_correlation(null_sink()));
}

namespace {

using tc_out_events = type_list<time_correlated_detection_event<>>;

} // namespace

TEST_CASE("time correlate at start") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, time_correlate_at_start<>(capture_output<tc_out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<tc_out_events>(valcat, ctx, "out");

    in.handle(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{3, 0, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at stop") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, time_correlate_at_stop<>(capture_output<tc_out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<tc_out_events>(valcat, ctx, "out");

    in.handle(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{5, 1, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at midpoint") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("use stop channel") {
        auto in = feed_input(
            valcat, time_correlate_at_midpoint<>(capture_output<tc_out_events>(
                        ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(valcat, ctx, "out");

        in.handle(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4, 1, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input(
            valcat, time_correlate_at_midpoint<default_data_types, true>(
                        capture_output<tc_out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(valcat, ctx, "out");

        in.handle(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4, 0, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("time correlate at fraction") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("use stop channel") {
        auto in = feed_input(
            valcat, time_correlate_at_fraction<>(
                        arg::fraction{1.0 / 3.0},
                        capture_output<tc_out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(valcat, ctx, "out");

        in.handle(std::array<detection_event<>, 2>{{{3000, 0}, {6000, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4000, 1, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input(
            valcat, time_correlate_at_fraction<default_data_types, true>(
                        arg::fraction{1.0 / 3.0},
                        capture_output<tc_out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(valcat, ctx, "out");

        in.handle(std::array<detection_event<>, 2>{{{3000, 0}, {6000, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4000, 0, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("negate difftime") {
    struct types : default_data_types {
        using difftime_type = std::int16_t;
    };
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        negate_difftime<types>(
            capture_output<type_list<time_correlated_detection_event<types>>>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<
        type_list<time_correlated_detection_event<types>>>(valcat, ctx, "out");

    in.handle(time_correlated_detection_event<types>{3, 1, 2});
    REQUIRE(out.check(time_correlated_detection_event<types>{3, 1, -2}));
    in.handle(time_correlated_detection_event<types>{5, 1, -7});
    REQUIRE(out.check(time_correlated_detection_event<types>{5, 1, 7}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("remove time correlation") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        remove_time_correlation<>(capture_output<type_list<detection_event<>>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<detection_event<>>>(
        valcat, ctx, "out");

    in.handle(time_correlated_detection_event<>{3, 1, 2});
    REQUIRE(out.check(detection_event<>{3, 1}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
