/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogram.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/data_types.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/histogram_policies.hpp"
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
#include <type_traits>
#include <vector>

namespace tcspc {

namespace {

using hp = histogram_policy;

using reset_event = empty_test_event<0>;
using misc_event = empty_test_event<1>;

struct data_types : default_data_types {
    using bin_index_type = u32;
    using bin_type = u16;
};

} // namespace

TEMPLATE_TEST_CASE_SIG("type constraints: histogram", "", ((hp P), P),
                       hp::error_on_overflow, hp::stop_on_overflow,
                       hp::saturate_on_overflow, hp::reset_on_overflow) {
    using base_output_events = type_list<histogram_event<>, misc_event>;
    using output_events = type_list_union_t<
        base_output_events,
        std::conditional_t<P == hp::saturate_on_overflow,
                           type_list<warning_event>, type_list<>>>;

    SECTION("no concluding event") {
        using proc_type = decltype(histogram<P, reset_event>(
            arg::num_bins<std::size_t>{64}, arg::max_per_bin<u16>{255},
            new_delete_bucket_source<u16>::create(),
            sink_event_list<output_events>()));
        STATIC_CHECK(is_processor_v<proc_type, bin_increment_event<>,
                                    reset_event, misc_event>);
        STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
    }

    if constexpr (P != hp::saturate_on_overflow) {
        SECTION("with concluding event") {
            using output_events_with_concluding =
                type_list_union_t<output_events,
                                  type_list<concluding_histogram_event<>>>;
            using proc_type =
                decltype(histogram<P | hp::emit_concluding_events,
                                   reset_event>(
                    arg::num_bins<std::size_t>{64}, arg::max_per_bin<u16>{255},
                    new_delete_bucket_source<u16>::create(),
                    sink_event_list<output_events_with_concluding>()));
            STATIC_CHECK(is_processor_v<proc_type, bin_increment_event<>,
                                        reset_event, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
    }
}

TEST_CASE("introspect: histogram") {
    check_introspect_simple_processor(
        histogram(arg::num_bins<std::size_t>{1}, arg::max_per_bin<u16>{255},
                  new_delete_bucket_source<u16>::create(), null_sink()));
}

namespace {

using all_output_events =
    type_list<histogram_event<>, concluding_histogram_event<>, warning_event,
              misc_event>;

}

TEMPLATE_TEST_CASE_SIG(
    "histogram normal operation without bin overflow or reset", "",
    ((hp P), P), hp::error_on_overflow, hp::stop_on_overflow,
    hp::saturate_on_overflow, hp::reset_on_overflow,
    hp::error_on_overflow | hp::emit_concluding_events,
    hp::stop_on_overflow | hp::emit_concluding_events,
    hp::saturate_on_overflow | hp::emit_concluding_events,
    hp::reset_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         histogram<P, reset_event>(
                             arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{65535}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    in.handle(misc_event{});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{}));

    SECTION("end before scan 0") {}

    SECTION("end after some increments") {
        in.handle(bin_increment_event<>{0});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({1, 0})}));

        in.handle(bin_increment_event<>{1});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({1, 1})}));

        in.handle(bin_increment_event<>{0});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({2, 1})}));
    }

    in.flush();
    CHECK(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG("histogram reset by event", "", ((hp P), P),
                       hp::error_on_overflow, hp::stop_on_overflow,
                       hp::saturate_on_overflow, hp::reset_on_overflow,
                       hp::error_on_overflow | hp::emit_concluding_events,
                       hp::stop_on_overflow | hp::emit_concluding_events,
                       hp::saturate_on_overflow | hp::emit_concluding_events,
                       hp::reset_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         histogram<P, reset_event>(
                             arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{65535}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    SECTION("reset at beginning") {
        in.handle(reset_event{});
        if constexpr (emit_concluding) {
            REQUIRE(out.check(
                emitted_as::always_rvalue,
                concluding_histogram_event<>{test_bucket<u16>({0, 0})}));
        }

        in.handle(bin_increment_event<>{1});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({0, 1})}));
    }

    SECTION("feed a few bin increments") {
        in.handle(bin_increment_event<>{1});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({0, 1})}));

        in.handle(bin_increment_event<>{0});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({1, 1})}));

        in.handle(bin_increment_event<>{1});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({1, 2})}));

        SECTION("reset") {
            in.handle(reset_event{});
            if constexpr (emit_concluding) {
                REQUIRE(out.check(
                    emitted_as::always_rvalue,
                    concluding_histogram_event<>{test_bucket<u16>({1, 2})}));
            }

            in.handle(bin_increment_event<>{1});
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_event<>{test_bucket<u16>({0, 1})}));
        }
    }

    in.flush();
    CHECK(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG("histogram error_on_overflow", "", ((hp P), P),
                       hp::error_on_overflow,
                       hp::error_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         histogram<P, reset_event>(
                             arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{3}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({1, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({2, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({3, 0})}));

    // Overflow.
    REQUIRE_THROWS_AS(in.handle(bin_increment_event<>{0}),
                      histogram_overflow_error);
    CHECK(out.check_not_flushed());
}

TEMPLATE_TEST_CASE_SIG("histogram stop_on_overflow", "", ((hp P), P),
                       hp::stop_on_overflow,
                       hp::stop_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         histogram<P, reset_event>(
                             arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{3}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({1, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({2, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({3, 0})}));

    // Overflow.
    REQUIRE_THROWS_AS(in.handle(bin_increment_event<>{0}), end_of_processing);
    if constexpr (emit_concluding) {
        REQUIRE(
            out.check(emitted_as::always_rvalue,
                      concluding_histogram_event<>{test_bucket<u16>({3, 0})}));
    }
    CHECK(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG("histogram saturate_on_overflow", "", ((hp P), P),
                       hp::saturate_on_overflow,
                       hp::saturate_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         histogram<P, reset_event>(
                             arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{3}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({1, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({2, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({3, 0})}));

    SECTION("saturate") {
        in.handle(bin_increment_event<>{0});
        REQUIRE(out.check(warning_event{"histogram bin saturated"}));
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_event<>{test_bucket<u16>({3, 0})}));

        SECTION("end") {}

        SECTION("second saturation") {
            in.handle(bin_increment_event<>{0});
            // No more warning.
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_event<>{test_bucket<u16>({3, 0})}));

            SECTION("end") {}

            SECTION("saturation after reset") {
                in.handle(reset_event{});
                if constexpr (emit_concluding) {
                    REQUIRE(out.check(concluding_histogram_event<>{
                        test_bucket<u16>({3, 0})}));
                }

                in.handle(bin_increment_event<>{0});
                REQUIRE(
                    out.check(emitted_as::always_lvalue,
                              histogram_event<>{test_bucket<u16>({1, 0})}));
                in.handle(bin_increment_event<>{0});
                REQUIRE(
                    out.check(emitted_as::always_lvalue,
                              histogram_event<>{test_bucket<u16>({2, 0})}));
                in.handle(bin_increment_event<>{0});
                REQUIRE(
                    out.check(emitted_as::always_lvalue,
                              histogram_event<>{test_bucket<u16>({3, 0})}));

                in.handle(bin_increment_event<>{0});
                REQUIRE(out.check(warning_event{"histogram bin saturated"}));
                REQUIRE(
                    out.check(emitted_as::always_lvalue,
                              histogram_event<>{test_bucket<u16>({3, 0})}));
            }
        }
    }

    in.flush();
    CHECK(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG("histogram reset_on_overflow", "", ((hp P), P),
                       hp::reset_on_overflow,
                       hp::reset_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         histogram<P, reset_event>(
                             arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{3}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({1, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({2, 0})}));
    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({3, 0})}));

    in.handle(bin_increment_event<>{0});
    if constexpr (emit_concluding) {
        REQUIRE(
            out.check(emitted_as::always_rvalue,
                      concluding_histogram_event<>{test_bucket<u16>({3, 0})}));
    }
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({1, 0})}));

    in.handle(bin_increment_event<>{0});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_event<>{test_bucket<u16>({2, 0})}));

    in.flush();
    CHECK(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG("histogram reset_on_overflow with max_per_bin = 0", "",
                       ((hp P), P), hp::reset_on_overflow,
                       hp::reset_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         histogram<P, reset_event>(
                             arg::num_bins<std::size_t>{2},
                             arg::max_per_bin<u16>{0}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    REQUIRE_THROWS_AS(in.handle(bin_increment_event<>{0}),
                      histogram_overflow_error);
    CHECK(out.check_not_flushed());
}

} // namespace tcspc
