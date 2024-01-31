/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/time_correlate.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using tc_out_events = event_set<time_correlated_detection_event<>>;

}

TEST_CASE("time correlate at start") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_pair_event<>>>(
        time_correlate_at_start<>(capture_output<tc_out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<tc_out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{{{3}, 0}, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at stop") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_pair_event<>>>(
        time_correlate_at_stop<>(capture_output<tc_out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<tc_out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{{{5}, 1}, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at midpoint") {
    auto ctx = std::make_shared<processor_context>();

    SECTION("use stop channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_midpoint<>(capture_output<tc_out_events>(
                ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{{{4}, 1}, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_midpoint<default_data_traits, true>(
                capture_output<tc_out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{{{4}, 0}, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("time correlate at fraction") {
    auto ctx = std::make_shared<processor_context>();

    SECTION("use stop channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_fraction<>(
                1.0 / 3.0, capture_output<tc_out_events>(
                               ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(detection_pair_event<>{{{{3000}, 0}}, {{{6000}, 1}}});
        REQUIRE(
            out.check(time_correlated_detection_event<>{{{4000}, 1}, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_fraction<default_data_traits, true>(
                1.0 / 3.0, capture_output<tc_out_events>(
                               ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<tc_out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(detection_pair_event<>{{{{3000}, 0}}, {{{6000}, 1}}});
        REQUIRE(
            out.check(time_correlated_detection_event<>{{{4000}, 0}, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("negate difftime") {
    struct traits : default_data_traits {
        using difftime_type = std::int16_t;
    };
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<time_correlated_detection_event<traits>>>(
        negate_difftime(
            capture_output<event_set<time_correlated_detection_event<traits>>>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<
        event_set<time_correlated_detection_event<traits>>>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(time_correlated_detection_event<traits>{{{3}, 1}, 2});
    REQUIRE(out.check(time_correlated_detection_event<traits>{{{3}, 1}, -2}));
    in.feed(time_correlated_detection_event<traits>{{{5}, 1}, -7});
    REQUIRE(out.check(time_correlated_detection_event<traits>{{{5}, 1}, 7}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("remove time correlation") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<time_correlated_detection_event<>>>(
        remove_time_correlation<>(capture_output<event_set<detection_event<>>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<event_set<detection_event<>>>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(time_correlated_detection_event<>{{{3}, 1}, 2});
    REQUIRE(out.check(detection_event<>{{{3}, 1}}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
