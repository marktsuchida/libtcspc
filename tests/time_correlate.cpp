/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/time_correlate.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

namespace tcspc {

namespace {

using tc_out_events = type_list<time_correlated_detection_event<>>;

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

TEST_CASE("time correlate at start") {
    auto ctx = context::create();
    auto in = feed_input<type_list<std::array<detection_event<>, 2>>>(
        time_correlate_at_start<>(capture_output<tc_out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<tc_out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{3, 0, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at stop") {
    auto ctx = context::create();
    auto in = feed_input<type_list<std::array<detection_event<>, 2>>>(
        time_correlate_at_stop<>(capture_output<tc_out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<tc_out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{5, 1, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at midpoint") {
    auto ctx = context::create();

    SECTION("use stop channel") {
        auto in = feed_input<type_list<std::array<detection_event<>, 2>>>(
            time_correlate_at_midpoint<>(capture_output<tc_out_events>(
                ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4, 1, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input<type_list<std::array<detection_event<>, 2>>>(
            time_correlate_at_midpoint<default_data_types, true>(
                capture_output<tc_out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(std::array<detection_event<>, 2>{{{3, 0}, {5, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4, 0, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("time correlate at fraction") {
    auto ctx = context::create();

    SECTION("use stop channel") {
        auto in = feed_input<type_list<std::array<detection_event<>, 2>>>(
            time_correlate_at_fraction<>(
                arg::fraction{1.0 / 3.0},
                capture_output<tc_out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(std::array<detection_event<>, 2>{{{3000, 0}, {6000, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4000, 1, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input<type_list<std::array<detection_event<>, 2>>>(
            time_correlate_at_fraction<default_data_types, true>(
                arg::fraction{1.0 / 3.0},
                capture_output<tc_out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(std::array<detection_event<>, 2>{{{3000, 0}, {6000, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{4000, 0, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("negate difftime") {
    struct types : default_data_types {
        using difftime_type = std::int16_t;
    };
    auto ctx = context::create();
    auto in = feed_input<type_list<time_correlated_detection_event<types>>>(
        negate_difftime(
            capture_output<type_list<time_correlated_detection_event<types>>>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<
        type_list<time_correlated_detection_event<types>>>(
        ctx->access<capture_output_access>("out"));

    in.feed(time_correlated_detection_event<types>{3, 1, 2});
    REQUIRE(out.check(time_correlated_detection_event<types>{3, 1, -2}));
    in.feed(time_correlated_detection_event<types>{5, 1, -7});
    REQUIRE(out.check(time_correlated_detection_event<types>{5, 1, 7}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("remove time correlation") {
    auto ctx = context::create();
    auto in = feed_input<type_list<time_correlated_detection_event<>>>(
        remove_time_correlation<>(capture_output<type_list<detection_event<>>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<detection_event<>>>(
        ctx->access<capture_output_access>("out"));

    in.feed(time_correlated_detection_event<>{3, 1, 2});
    REQUIRE(out.check(detection_event<>{3, 1}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
