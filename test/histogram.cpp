/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogram.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/test_utils.hpp"

#include <cstdint>

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using reset_event = timestamped_test_event<0>;
using misc_event = timestamped_test_event<1>;

struct data_traits : default_data_traits {
    using bin_index_type = u32;
    using bin_type = u16;
};

} // namespace

TEMPLATE_TEST_CASE("Histogram, zero bins", "[histogram]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    using out_events =
        event_set<histogram_event<data_traits>,
                  concluding_histogram_event<data_traits>, misc_event>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<
        event_set<bin_increment_event<data_traits>, reset_event, misc_event>>(
        histogram<reset_event, TestType, data_traits>(
            0, 0,
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed(reset_event{});
    REQUIRE(out.check(
        concluding_histogram_event<data_traits>{{}, {}, {0, 0}, 0, false}));
    in.flush();
    REQUIRE(out.check(
        concluding_histogram_event<data_traits>{{}, {}, {0, 0}, 0, true}));
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE("Histogram, no overflow", "[histogram]",
                   saturate_on_overflow, reset_on_overflow, stop_on_overflow,
                   error_on_overflow) {
    using out_events = event_set<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();
    auto in =
        feed_input<event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, TestType, data_traits>(
                2, 100,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    std::vector<u16> hist;
    in.feed(bin_increment_event<data_traits>{42, 0});
    hist = {1, 0};
    REQUIRE(out.check(
        histogram_event<data_traits>{{42, 42}, autocopy_span(hist), {1, 0}}));
    in.feed(bin_increment_event<data_traits>{43, 1});
    hist = {1, 1};
    REQUIRE(out.check(
        histogram_event<data_traits>{{42, 43}, autocopy_span(hist), {2, 0}}));
    in.feed(reset_event{44});
    hist = {1, 1};
    REQUIRE(out.check(concluding_histogram_event<data_traits>{
        {42, 43}, autocopy_span(hist), {2, 0}, 0, false}));
    in.feed(bin_increment_event<data_traits>{45, 0});
    hist = {1, 0};
    REQUIRE(out.check(
        histogram_event<data_traits>{{45, 45}, autocopy_span(hist), {1, 0}}));
    in.flush();
    hist = {1, 0};
    REQUIRE(out.check(concluding_histogram_event<data_traits>{
        {45, 45}, autocopy_span(hist), {1, 0}, 0, true}));
    REQUIRE(out.check_flushed());
}

TEST_CASE("Histogram, saturate on overflow", "[histogram]") {
    using out_events = event_set<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, saturate_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0}); // Overflow
        hist = {0};
        REQUIRE(out.check(histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 1}}));
        in.flush();
        hist = {0};
        REQUIRE(out.check(concluding_histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 1}, 0, true}));
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, saturate_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(out.check(histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 0}}));
        in.feed(bin_increment_event<data_traits>{43, 0}); // Overflow
        hist = {1};
        REQUIRE(out.check(histogram_event<data_traits>{
            {42, 43}, autocopy_span(hist), {2, 1}}));
        in.feed(reset_event{44});
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<data_traits>{
            {42, 43}, autocopy_span(hist), {2, 1}, 0, false}));
        in.feed(bin_increment_event<data_traits>{45, 0});
        hist = {1};
        REQUIRE(out.check(histogram_event<data_traits>{
            {45, 45}, autocopy_span(hist), {1, 0}}));
        in.flush();
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<data_traits>{
            {45, 45}, autocopy_span(hist), {1, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, reset on overflow", "[histogram]") {
    using out_events = event_set<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, reset_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{42, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, reset_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(out.check(histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 0}}));
        in.feed(bin_increment_event<data_traits>{43, 0}); // Overflow
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 0}, 0, false}));
        hist = {1};
        REQUIRE(out.check(histogram_event<data_traits>{
            {43, 43}, autocopy_span(hist), {1, 0}}));
        in.flush();
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<data_traits>{
            {43, 43}, autocopy_span(hist), {1, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, stop on overflow", "[histogram]") {
    using out_events = event_set<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();
    std::vector<u16> hist;

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, stop_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{42, 0}),
                          end_processing); // Overflow
        hist = {0};
        REQUIRE(out.check(concluding_histogram_event<data_traits>{
            {}, autocopy_span(hist), {0, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, stop_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(out.check(histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 0}}));
        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{43, 0}),
                          end_processing); // Overflow
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, error on overflow", "[histogram]") {
    using out_events = event_set<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, error_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{42, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            event_set<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, error_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(out.check(histogram_event<data_traits>{
            {42, 42}, autocopy_span(hist), {1, 0}}));
        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{43, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }
}

} // namespace tcspc
