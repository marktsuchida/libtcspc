/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogram.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace tcspc {

namespace {

using reset_event = empty_test_event<0>;
using misc_event = empty_test_event<1>;

struct data_types : default_data_types {
    using bin_index_type = u32;
    using bin_type = u16;
};

template <typename T> auto tmp_bucket(T &v) {
    struct ignore_storage {};
    return bucket(span(v), ignore_storage{});
}

} // namespace

TEST_CASE("histogram event type constraints") {
    SECTION("saturate on overflow") {
        SECTION("no reset") {
            using proc_type = decltype(histogram<reset_event>(
                saturate_on_overflow, arg::num_bins<std::size_t>{1},
                arg::max_per_bin<u16>{255},
                new_delete_bucket_source<u16>::create(),
                sink_events<histogram_event<>, warning_event, misc_event>()));
            STATIC_CHECK(
                is_processor_v<proc_type, bin_increment_event<>, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, reset_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
        SECTION("with reset") {
            using proc_type = decltype(histogram<reset_event>(
                saturate_on_overflow, arg::num_bins<std::size_t>{1},
                arg::max_per_bin<u16>{255},
                new_delete_bucket_source<u16>::create(),
                sink_events<histogram_event<>, concluding_histogram_event<>,
                            warning_event, misc_event>()));
            STATIC_CHECK(is_processor_v<proc_type, bin_increment_event<>,
                                        reset_event, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
    }

    SECTION("reset on overflow") {
        using proc_type = decltype(histogram<reset_event>(
            reset_on_overflow, arg::num_bins<std::size_t>{1},
            arg::max_per_bin<u16>{255},
            new_delete_bucket_source<u16>::create(),
            sink_events<histogram_event<>, concluding_histogram_event<>,
                        misc_event>()));
        STATIC_CHECK(is_processor_v<proc_type, bin_increment_event<>,
                                    reset_event, misc_event>);
        STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
    }

    SECTION("stop on overflow") {
        SECTION("no reset") {
            using proc_type = decltype(histogram<reset_event>(
                stop_on_overflow, arg::num_bins<std::size_t>{1},
                arg::max_per_bin<u16>{255},
                new_delete_bucket_source<u16>::create(),
                sink_events<histogram_event<>, misc_event>()));
            STATIC_CHECK(
                is_processor_v<proc_type, bin_increment_event<>, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, reset_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
        SECTION("with reset") {
            using proc_type = decltype(histogram<reset_event>(
                stop_on_overflow, arg::num_bins<std::size_t>{1},
                arg::max_per_bin<u16>{255},
                new_delete_bucket_source<u16>::create(),
                sink_events<histogram_event<>, concluding_histogram_event<>,
                            misc_event>()));
            STATIC_CHECK(is_processor_v<proc_type, bin_increment_event<>,
                                        reset_event, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
    }

    SECTION("error on overflow") {
        SECTION("no reset") {
            using proc_type = decltype(histogram<reset_event>(
                error_on_overflow, arg::num_bins<std::size_t>{1},
                arg::max_per_bin<u16>{255},
                new_delete_bucket_source<u16>::create(),
                sink_events<histogram_event<>, misc_event>()));
            STATIC_CHECK(
                is_processor_v<proc_type, bin_increment_event<>, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, reset_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
        SECTION("with reset") {
            using proc_type = decltype(histogram<reset_event>(
                error_on_overflow, arg::num_bins<std::size_t>{1},
                arg::max_per_bin<u16>{255},
                new_delete_bucket_source<u16>::create(),
                sink_events<histogram_event<>, concluding_histogram_event<>,
                            misc_event>()));
            STATIC_CHECK(is_processor_v<proc_type, bin_increment_event<>,
                                        reset_event, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
    }
}

TEST_CASE("introspect histogram", "[introspect]") {
    check_introspect_simple_processor(histogram<reset_event>(
        saturate_on_overflow, arg::num_bins<std::size_t>{1},
        arg::max_per_bin<u16>{255}, new_delete_bucket_source<u16>::create(),
        null_sink()));
}

TEMPLATE_TEST_CASE("Histogram, no overflow", "", saturate_on_overflow_t,
                   reset_on_overflow_t, stop_on_overflow_t,
                   error_on_overflow_t) {
    using out_events =
        type_list<histogram_event<data_types>,
                  concluding_histogram_event<data_types>, warning_event>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         histogram<reset_event, data_types>(
                             TestType{}, arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{100},
                             new_delete_bucket_source<u16>::create(),
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    std::vector<u16> hist;
    in.handle(bin_increment_event<data_types>{0});
    hist = {1, 0};
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<data_types>{tmp_bucket(hist)}));
    in.handle(bin_increment_event<data_types>{1});
    hist = {1, 1};
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<data_types>{tmp_bucket(hist)}));
    in.handle(reset_event{});
    hist = {1, 1};
    REQUIRE(
        out.check(emitted_as::always_rvalue,
                  concluding_histogram_event<data_types>{tmp_bucket(hist)}));
    in.handle(bin_increment_event<data_types>{0});
    hist = {1, 0};
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<data_types>{tmp_bucket(hist)}));
    in.handle(reset_event{});
    hist = {1, 0};
    REQUIRE(
        out.check(emitted_as::always_rvalue,
                  concluding_histogram_event<data_types>{tmp_bucket(hist)}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("Histogram, saturate on overflow") {
    using out_events =
        type_list<histogram_event<data_types>,
                  concluding_histogram_event<data_types>, warning_event>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("Max per bin = 0") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        saturate_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{0},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        std::vector<u16> hist;
        in.handle(bin_increment_event<data_types>{0}); // Overflow
        REQUIRE(out.check(warning_event{"histogram saturated"}));
        hist = {0};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        in.handle(reset_event{});
        hist = {0};
        REQUIRE(out.check(
            emitted_as::always_rvalue,
            concluding_histogram_event<data_types>{tmp_bucket(hist)}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        saturate_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{1},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        std::vector<u16> hist;
        in.handle(bin_increment_event<data_types>{0});
        hist = {1};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        in.handle(bin_increment_event<data_types>{0}); // Overflow
        REQUIRE(out.check(warning_event{"histogram saturated"}));
        hist = {1};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        in.handle(reset_event{});
        hist = {1};
        REQUIRE(out.check(
            emitted_as::always_rvalue,
            concluding_histogram_event<data_types>{tmp_bucket(hist)}));
        in.handle(bin_increment_event<data_types>{0});
        hist = {1};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        in.handle(reset_event{});
        hist = {1};
        REQUIRE(out.check(
            emitted_as::always_rvalue,
            concluding_histogram_event<data_types>{tmp_bucket(hist)}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, reset on overflow") {
    using out_events = type_list<histogram_event<data_types>,
                                 concluding_histogram_event<data_types>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("Max per bin = 0") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        reset_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{0},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        REQUIRE_THROWS_AS(in.handle(bin_increment_event<data_types>{0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        reset_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{1},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        std::vector<u16> hist;
        in.handle(bin_increment_event<data_types>{0});
        hist = {1};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        in.handle(bin_increment_event<data_types>{0}); // Overflow
        hist = {1};
        REQUIRE(out.check(
            emitted_as::always_rvalue,
            concluding_histogram_event<data_types>{tmp_bucket(hist)}));
        hist = {1};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        in.handle(reset_event{});
        hist = {1};
        REQUIRE(out.check(
            emitted_as::always_rvalue,
            concluding_histogram_event<data_types>{tmp_bucket(hist)}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, stop on overflow") {
    using out_events = type_list<histogram_event<data_types>,
                                 concluding_histogram_event<data_types>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    std::vector<u16> hist;

    SECTION("Max per bin = 0") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        stop_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{0},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        REQUIRE_THROWS_AS(in.handle(bin_increment_event<data_types>{0}),
                          end_of_processing); // Overflow
        hist = {0};
        REQUIRE(out.check(
            emitted_as::always_rvalue,
            concluding_histogram_event<data_types>{tmp_bucket(hist)}));
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        stop_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{1},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.handle(bin_increment_event<data_types>{0});
        hist = {1};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        REQUIRE_THROWS_AS(in.handle(bin_increment_event<data_types>{0}),
                          end_of_processing); // Overflow
        hist = {1};
        REQUIRE(out.check(
            emitted_as::always_rvalue,
            concluding_histogram_event<data_types>{tmp_bucket(hist)}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram, error on overflow") {
    using out_events = type_list<histogram_event<data_types>,
                                 concluding_histogram_event<data_types>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("Max per bin = 0") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        error_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{0},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        REQUIRE_THROWS_AS(in.handle(bin_increment_event<data_types>{0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input(
            valcat, histogram<reset_event, data_types>(
                        error_on_overflow, arg::num_bins<std::size_t>{1},
                        arg::max_per_bin<u16>{1},
                        new_delete_bucket_source<u16>::create(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        std::vector<u16> hist;
        in.handle(bin_increment_event<data_types>{0});
        hist = {1};
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<data_types>{tmp_bucket(hist)}));
        REQUIRE_THROWS_AS(in.handle(bin_increment_event<data_types>{0}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }
}

} // namespace tcspc
