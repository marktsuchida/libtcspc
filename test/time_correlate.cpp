/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/time_correlate.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("time correlate at start", "[time_correlate_at_start]") {
    auto out = capture_output<event_set<time_correlated_detection_event<>>>();
    auto in = feed_input<event_set<detection_pair_event<>>>(
        time_correlate_at_start<>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{{{3}, 0}, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at stop", "[time_correlate_at_stop]") {
    auto out = capture_output<event_set<time_correlated_detection_event<>>>();
    auto in = feed_input<event_set<detection_pair_event<>>>(
        time_correlate_at_stop<>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
    REQUIRE(out.check(time_correlated_detection_event<>{{{5}, 1}, 2}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("time correlate at midpoint", "[time_correlate_at_midpoint]") {
    auto out = capture_output<event_set<time_correlated_detection_event<>>>();

    SECTION("use stop channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_midpoint<>(ref_processor(out)));
        in.require_output_checked(out);

        in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{{{4}, 1}, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_midpoint<default_data_traits, true>(
                ref_processor(out)));
        in.require_output_checked(out);

        in.feed(detection_pair_event<>{{{{3}, 0}}, {{{5}, 1}}});
        REQUIRE(out.check(time_correlated_detection_event<>{{{4}, 0}, 2}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("time correlate at fraction", "[time_correlate_at_fraction]") {
    auto out = capture_output<event_set<time_correlated_detection_event<>>>();

    SECTION("use stop channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_fraction<>(1.0 / 3.0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(detection_pair_event<>{{{{3000}, 0}}, {{{6000}, 1}}});
        REQUIRE(
            out.check(time_correlated_detection_event<>{{{4000}, 1}, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("use start channel") {
        auto in = feed_input<event_set<detection_pair_event<>>>(
            time_correlate_at_fraction<default_data_traits, true>(
                1.0 / 3.0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(detection_pair_event<>{{{{3000}, 0}}, {{{6000}, 1}}});
        REQUIRE(
            out.check(time_correlated_detection_event<>{{{4000}, 0}, 3000}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("negate difftime", "[negate_difftime]") {
    struct traits : default_data_traits {
        using difftime_type = std::int16_t;
    };
    auto out =
        capture_output<event_set<time_correlated_detection_event<traits>>>();
    auto in = feed_input<event_set<time_correlated_detection_event<traits>>>(
        negate_difftime(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(time_correlated_detection_event<traits>{{{3}, 1}, 2});
    REQUIRE(out.check(time_correlated_detection_event<traits>{{{3}, 1}, -2}));
    in.feed(time_correlated_detection_event<traits>{{{5}, 1}, -7});
    REQUIRE(out.check(time_correlated_detection_event<traits>{{{5}, 1}, 7}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("remove time correlation", "[remove_time_correlation]") {
    auto out = capture_output<event_set<detection_event<>>>();
    auto in = feed_input<event_set<time_correlated_detection_event<>>>(
        remove_time_correlation<>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(time_correlated_detection_event<>{{{3}, 1}, 2});
    REQUIRE(out.check(detection_event<>{{{3}, 1}}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
