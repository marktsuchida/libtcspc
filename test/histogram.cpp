/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogram.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <cstdint>

#include <catch2/catch_all.hpp>

namespace tcspc {

using reset_event = timestamped_test_event<0>;
using misc_event = timestamped_test_event<1>;

TEMPLATE_TEST_CASE("Histogram, zero bins", "[histogram]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto out = capture_output<event_set<
        histogram_event<u16>, concluding_histogram_event<u16>, misc_event>>();
    auto in = feed_input<
        event_set<bin_increment_event<u32>, reset_event, misc_event>>(
        histogram<default_data_traits, u32, u16, reset_event, TestType>(
            0, 0, ref_processor(out)));
    in.require_output_checked(out);

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed(reset_event{});
    REQUIRE(
        out.check(concluding_histogram_event<u16>{{}, {}, {0, 0}, 0, false}));
    in.flush();
    REQUIRE(
        out.check(concluding_histogram_event<u16>{{}, {}, {0, 0}, 0, true}));
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE("Histogram, no overflow", "[histogram]",
                   saturate_on_overflow, reset_on_overflow, stop_on_overflow,
                   error_on_overflow) {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();
    auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
        histogram<default_data_traits, u32, u16, reset_event, TestType>(
            2, 100, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u16> hist;
    in.feed(bin_increment_event<u32>{42, 0});
    hist = {1, 0};
    REQUIRE(out.check(
        histogram_event<u16>{{42, 42}, autocopy_span(hist), {1, 0}}));
    in.feed(bin_increment_event<u32>{43, 1});
    hist = {1, 1};
    REQUIRE(out.check(
        histogram_event<u16>{{42, 43}, autocopy_span(hist), {2, 0}}));
    in.feed(reset_event{44});
    hist = {1, 1};
    REQUIRE(out.check(concluding_histogram_event<u16>{
        {42, 43}, autocopy_span(hist), {2, 0}, 0, false}));
    in.feed(bin_increment_event<u32>{45, 0});
    hist = {1, 0};
    REQUIRE(out.check(
        histogram_event<u16>{{45, 45}, autocopy_span(hist), {1, 0}}));
    in.flush();
    hist = {1, 0};
    REQUIRE(out.check(concluding_histogram_event<u16>{
        {45, 45}, autocopy_span(hist), {1, 0}, 0, true}));
    REQUIRE(out.check_flushed());
}

TEST_CASE("Histogram, saturate on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      saturate_on_overflow>(1, 0, ref_processor(out)));
        in.require_output_checked(out);

        std::vector<u16> hist;
        in.feed(bin_increment_event<u32>{42, 0}); // Overflow
        hist = {0};
        REQUIRE(out.check(
            histogram_event<u16>{{42, 42}, autocopy_span(hist), {1, 1}}));
        in.flush();
        hist = {0};
        REQUIRE(out.check(concluding_histogram_event<u16>{
            {42, 42}, autocopy_span(hist), {1, 1}, 0, true}));
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      saturate_on_overflow>(1, 1, ref_processor(out)));
        in.require_output_checked(out);

        std::vector<u16> hist;
        in.feed(bin_increment_event<u32>{42, 0});
        hist = {1};
        REQUIRE(out.check(
            histogram_event<u16>{{42, 42}, autocopy_span(hist), {1, 0}}));
        in.feed(bin_increment_event<u32>{43, 0}); // Overflow
        hist = {1};
        REQUIRE(out.check(
            histogram_event<u16>{{42, 43}, autocopy_span(hist), {2, 1}}));
        in.feed(reset_event{44});
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<u16>{
            {42, 43}, autocopy_span(hist), {2, 1}, 0, false}));
        in.feed(bin_increment_event<u32>{45, 0});
        hist = {1};
        REQUIRE(out.check(
            histogram_event<u16>{{45, 45}, autocopy_span(hist), {1, 0}}));
        in.flush();
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<u16>{
            {45, 45}, autocopy_span(hist), {1, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, reset on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      reset_on_overflow>(1, 0, ref_processor(out)));
        in.require_output_checked(out);

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<u32>{42, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      reset_on_overflow>(1, 1, ref_processor(out)));
        in.require_output_checked(out);

        std::vector<u16> hist;
        in.feed(bin_increment_event<u32>{42, 0});
        hist = {1};
        REQUIRE(out.check(
            histogram_event<u16>{{42, 42}, autocopy_span(hist), {1, 0}}));
        in.feed(bin_increment_event<u32>{43, 0}); // Overflow
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<u16>{
            {42, 42}, autocopy_span(hist), {1, 0}, 0, false}));
        hist = {1};
        REQUIRE(out.check(
            histogram_event<u16>{{43, 43}, autocopy_span(hist), {1, 0}}));
        in.flush();
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<u16>{
            {43, 43}, autocopy_span(hist), {1, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, stop on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();
    std::vector<u16> hist;

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      stop_on_overflow>(1, 0, ref_processor(out)));
        in.require_output_checked(out);

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<u32>{42, 0}),
                          end_processing); // Overflow
        hist = {0};
        REQUIRE(out.check(concluding_histogram_event<u16>{
            {}, autocopy_span(hist), {0, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      stop_on_overflow>(1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0});
        hist = {1};
        REQUIRE(out.check(
            histogram_event<u16>{{42, 42}, autocopy_span(hist), {1, 0}}));
        REQUIRE_THROWS_AS(in.feed(bin_increment_event<u32>{43, 0}),
                          end_processing); // Overflow
        hist = {1};
        REQUIRE(out.check(concluding_histogram_event<u16>{
            {42, 42}, autocopy_span(hist), {1, 0}, 0, true}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, error on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      error_on_overflow>(1, 0, ref_processor(out)));
        in.require_output_checked(out);

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<u32>{42, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<default_data_traits, u32, u16, reset_event,
                      error_on_overflow>(1, 1, ref_processor(out)));
        in.require_output_checked(out);

        std::vector<u16> hist;
        in.feed(bin_increment_event<u32>{42, 0});
        hist = {1};
        REQUIRE(out.check(
            histogram_event<u16>{{42, 42}, autocopy_span(hist), {1, 0}}));
        REQUIRE_THROWS_AS(in.feed(bin_increment_event<u32>{43, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }
}

} // namespace tcspc
