/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogram.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/own_on_copy_view.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace tcspc {

namespace {

using reset_event = timestamped_test_event<0>;
using misc_event = timestamped_test_event<1>;

struct data_traits : default_data_traits {
    using bin_index_type = u32;
    using bin_type = u16;
};

} // namespace

TEST_CASE("introspect histogram", "[introspect]") {
    check_introspect_simple_processor(
        histogram<reset_event, saturate_on_overflow>(1, 255, null_sink()));
}

TEMPLATE_TEST_CASE("Histogram, zero bins", "", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    using out_events = type_list<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>,
                                 warning_event, misc_event>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<
        type_list<bin_increment_event<data_traits>, reset_event, misc_event>>(
        histogram<reset_event, TestType, data_traits>(
            0, 0,
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed(reset_event{});
    REQUIRE(out.check(concluding_histogram_event<data_traits>{{}}));
    in.flush();
    REQUIRE(out.check(concluding_histogram_event<data_traits>{{}}));
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE("Histogram, no overflow", "", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    using out_events =
        type_list<histogram_event<data_traits>,
                  concluding_histogram_event<data_traits>, warning_event>;
    auto ctx = std::make_shared<processor_context>();
    auto in =
        feed_input<type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, TestType, data_traits>(
                2, 100,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    std::vector<u16> hist;
    in.feed(bin_increment_event<data_traits>{42, 0});
    hist = {1, 0};
    REQUIRE(out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
    in.feed(bin_increment_event<data_traits>{43, 1});
    hist = {1, 1};
    REQUIRE(out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
    in.feed(reset_event{44});
    hist = {1, 1};
    REQUIRE(out.check(
        concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
    in.feed(bin_increment_event<data_traits>{45, 0});
    hist = {1, 0};
    REQUIRE(out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
    in.flush();
    hist = {1, 0};
    REQUIRE(out.check(
        concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
    REQUIRE(out.check_flushed());
}

TEST_CASE("Histogram, saturate on overflow") {
    using out_events =
        type_list<histogram_event<data_traits>,
                  concluding_histogram_event<data_traits>, warning_event>;
    auto ctx = std::make_shared<processor_context>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, saturate_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0}); // Overflow
        REQUIRE(out.check(warning_event{"histogram saturated"}));
        hist = {0};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        in.flush();
        hist = {0};
        REQUIRE(out.check(
            concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, saturate_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        in.feed(bin_increment_event<data_traits>{43, 0}); // Overflow
        REQUIRE(out.check(warning_event{"histogram saturated"}));
        hist = {1};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        in.feed(reset_event{44});
        hist = {1};
        REQUIRE(out.check(
            concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
        in.feed(bin_increment_event<data_traits>{45, 0});
        hist = {1};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        in.flush();
        hist = {1};
        REQUIRE(out.check(
            concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, reset on overflow") {
    using out_events = type_list<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, reset_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{42, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, reset_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        in.feed(bin_increment_event<data_traits>{43, 0}); // Overflow
        hist = {1};
        REQUIRE(out.check(
            concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
        hist = {1};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        in.flush();
        hist = {1};
        REQUIRE(out.check(
            concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, stop on overflow") {
    using out_events = type_list<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();
    std::vector<u16> hist;

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, stop_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{42, 0}),
                          end_processing); // Overflow
        hist = {0};
        REQUIRE(out.check(
            concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, stop_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{43, 0}),
                          end_processing); // Overflow
        hist = {1};
        REQUIRE(out.check(
            concluding_histogram_event<data_traits>{own_on_copy_view(hist)}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, error on overflow") {
    using out_events = type_list<histogram_event<data_traits>,
                                 concluding_histogram_event<data_traits>>;
    auto ctx = std::make_shared<processor_context>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, error_on_overflow, data_traits>(
                1, 0,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{42, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<
            type_list<bin_increment_event<data_traits>, reset_event>>(
            histogram<reset_event, error_on_overflow, data_traits>(
                1, 1,
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        std::vector<u16> hist;
        in.feed(bin_increment_event<data_traits>{42, 0});
        hist = {1};
        REQUIRE(
            out.check(histogram_event<data_traits>{own_on_copy_view(hist)}));
        REQUIRE_THROWS_AS(in.feed(bin_increment_event<data_traits>{43, 0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }
}

} // namespace tcspc
