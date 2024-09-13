/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/scan_histograms.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/histogram_policy.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <type_traits>

namespace tcspc {

namespace {

using hp = histogram_policy;

using reset_event = empty_test_event<0>;
using misc_event = empty_test_event<1>;

} // namespace

TEMPLATE_TEST_CASE_SIG("type constraints: scan_histograms", "", ((hp P), P),
                       hp::error_on_overflow, hp::stop_on_overflow,
                       hp::saturate_on_overflow, hp::reset_on_overflow) {
    using base_output_events =
        type_list<histogram_array_progress_event<>, histogram_array_event<>,
                  reset_event, misc_event>;
    using output_events = type_list_union_t<
        base_output_events,
        std::conditional_t<P == hp::saturate_on_overflow,
                           type_list<warning_event>, type_list<>>>;

    SECTION("no concluding event") {
        using proc_type = decltype(scan_histograms<P, reset_event>(
            arg::num_elements<>{256}, arg::num_bins<>{256},
            arg::max_per_bin<u16>{255},
            new_delete_bucket_source<u16>::create(),
            sink_event_list<output_events>()));
        STATIC_CHECK(is_processor_v<proc_type, bin_increment_cluster_event<>,
                                    reset_event, misc_event>);
        STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
    }

    if constexpr (P != hp::saturate_on_overflow) {
        SECTION("with concluding event") {
            using output_events_with_concluding = type_list_union_t<
                output_events, type_list<concluding_histogram_array_event<>>>;
            using proc_type =
                decltype(scan_histograms<P | hp::emit_concluding_events,
                                         reset_event>(
                    arg::num_elements<>{256}, arg::num_bins<>{256},
                    arg::max_per_bin<u16>{255},
                    new_delete_bucket_source<u16>::create(),
                    sink_event_list<output_events_with_concluding>()));
            STATIC_CHECK(
                is_processor_v<proc_type, bin_increment_cluster_event<>,
                               reset_event, misc_event>);
            STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
        }
    }
}

TEST_CASE("introspect: scan_histograms") {
    check_introspect_simple_processor(scan_histograms(
        arg::num_elements<>{1}, arg::num_bins<>{1}, arg::max_per_bin<u16>{255},
        new_delete_bucket_source<u16>::create(), null_sink()));
}

namespace {

using all_output_events =
    type_list<histogram_array_progress_event<>, histogram_array_event<>,
              concluding_histogram_array_event<>, warning_event, reset_event,
              misc_event>;

}

TEMPLATE_TEST_CASE_SIG(
    "scan_histograms normal operation without bin overflow or reset", "",
    ((hp P), P), hp::error_on_overflow, hp::stop_on_overflow,
    hp::saturate_on_overflow, hp::reset_on_overflow,
    hp::error_on_overflow | hp::emit_concluding_events,
    hp::stop_on_overflow | hp::emit_concluding_events,
    // saturate_on_overflow doesn't support emit_concluding_events
    hp::reset_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         scan_histograms<P, reset_event>(
                             arg::num_elements<>{2}, arg::num_bins<>{2},
                             arg::max_per_bin<u16>{65535}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    in.handle(misc_event{});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{}));

    CHECK(bsource->bucket_count() == 0);

    SECTION("end before scan 0") {}

    SECTION("feed scan 0, element 0") {
        in.handle(
            bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 0})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({3, 1, 0, 0})}));
        CHECK(bsource->bucket_count() == 1);

        SECTION("end") {}

        SECTION("feed scan 0, element 1 (last element)") {
            in.handle(
                bin_increment_cluster_event<>{test_bucket<u16>({1, 1, 0})});
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_array_progress_event<>{
                                  4, test_bucket<u16>({3, 1, 1, 2})}));
            REQUIRE(out.check(
                emitted_as::always_lvalue,
                histogram_array_event<>{test_bucket<u16>({3, 1, 1, 2})}));

            SECTION("end") {}

            SECTION("feed scan 1, element 0") {
                in.handle(bin_increment_cluster_event<>{
                    test_bucket<u16>({0, 1, 0, 1})});
                REQUIRE(out.check(emitted_as::always_lvalue,
                                  histogram_array_progress_event<>{
                                      2, test_bucket<u16>({5, 3, 1, 2})}));

                SECTION("end") {}

                SECTION("feed scan 1, element 1 (last element)") {
                    in.handle(bin_increment_cluster_event<>{
                        test_bucket<u16>({0, 0, 0, 1})});
                    REQUIRE(out.check(emitted_as::always_lvalue,
                                      histogram_array_progress_event<>{
                                          4, test_bucket<u16>({5, 3, 4, 3})}));
                    REQUIRE(out.check(emitted_as::always_lvalue,
                                      histogram_array_event<>{
                                          test_bucket<u16>({5, 3, 4, 3})}));
                    CHECK(bsource->bucket_count() == 1);
                }
            }
        }
    }

    in.flush();
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG(
    "scan_histograms single element, single bin edge case", "", ((hp P), P),
    hp::error_on_overflow, hp::stop_on_overflow, hp::saturate_on_overflow,
    hp::reset_on_overflow, hp::error_on_overflow | hp::emit_concluding_events,
    hp::stop_on_overflow | hp::emit_concluding_events,
    // saturate_on_overflow doesn't support emit_concluding_events
    hp::reset_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         scan_histograms<P, reset_event>(
                             arg::num_elements<>{1}, arg::num_bins<>{1},
                             arg::max_per_bin<u16>{65535}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 0, 0})});
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_progress_event<>{1, test_bucket<u16>({3})}));
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_array_event<>{test_bucket<u16>({3})}));

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0})});
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_progress_event<>{1, test_bucket<u16>({4})}));
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_array_event<>{test_bucket<u16>({4})}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG(
    "scan_histograms reset_after_scan", "", ((hp P), P), hp::error_on_overflow,
    hp::stop_on_overflow, hp::saturate_on_overflow, hp::reset_on_overflow,
    hp::error_on_overflow | hp::emit_concluding_events,
    hp::stop_on_overflow | hp::emit_concluding_events,
    // saturate_on_overflow doesn't support emit_concluding_events
    hp::reset_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(
        valcat, scan_histograms<P | hp::reset_after_scan, reset_event>(
                    arg::num_elements<>{2}, arg::num_bins<>{2},
                    arg::max_per_bin<u16>{65535}, bsource,
                    capture_output<all_output_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    // Scan 0
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 0})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{2, test_bucket<u16>({3, 1, 0, 0})}));
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({1, 1, 0})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{4, test_bucket<u16>({3, 1, 1, 2})}));
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_event<>{test_bucket<u16>({3, 1, 1, 2})}));
    if constexpr (emit_concluding) {
        REQUIRE(out.check(emitted_as::always_rvalue,
                          concluding_histogram_array_event<>{
                              test_bucket<u16>({3, 1, 1, 2})}));
    }
    CHECK(bsource->bucket_count() == 1);

    // Scan 1
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 1})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{2, test_bucket<u16>({2, 2, 0, 0})}));
    CHECK(bsource->bucket_count() == 2);

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 0, 0, 1})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{4, test_bucket<u16>({2, 2, 3, 1})}));
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_event<>{test_bucket<u16>({2, 2, 3, 1})}));
    if constexpr (emit_concluding) {
        REQUIRE(out.check(emitted_as::always_rvalue,
                          concluding_histogram_array_event<>{
                              test_bucket<u16>({2, 2, 3, 1})}));
    }
    CHECK(bsource->bucket_count() == 2);

    in.flush();
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG(
    "scan_histograms clear_every_scan", "", ((hp P), P), hp::error_on_overflow,
    hp::stop_on_overflow, hp::saturate_on_overflow, hp::reset_on_overflow,
    hp::error_on_overflow | hp::emit_concluding_events,
    hp::stop_on_overflow | hp::emit_concluding_events,
    // saturate_on_overflow doesn't support emit_concluding_events
    hp::reset_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(
        valcat, scan_histograms<P | hp::clear_every_scan, reset_event>(
                    arg::num_elements<>{2}, arg::num_bins<>{2},
                    arg::max_per_bin<u16>{65535}, bsource,
                    capture_output<all_output_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    // Scan 0
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 0})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{2, test_bucket<u16>({3, 1, 0, 0})}));
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({1, 1, 0})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{4, test_bucket<u16>({3, 1, 1, 2})}));
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_event<>{test_bucket<u16>({3, 1, 1, 2})}));

    // Scan 1
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 1})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{2, test_bucket<u16>({2, 2, 1, 2})}));

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 0, 0, 1})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{4, test_bucket<u16>({2, 2, 3, 1})}));
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_event<>{test_bucket<u16>({2, 2, 3, 1})}));
    CHECK(bsource->bucket_count() == 1);

    in.flush();
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG(
    "scan_histograms no_clear_new_bucket", "", ((hp P), P),
    hp::error_on_overflow, hp::stop_on_overflow, hp::saturate_on_overflow,
    hp::reset_on_overflow, hp::error_on_overflow | hp::emit_concluding_events,
    hp::stop_on_overflow | hp::emit_concluding_events,
    // saturate_on_overflow doesn't support emit_concluding_events
    hp::reset_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(
        valcat, scan_histograms<P | hp::no_clear_new_bucket, reset_event>(
                    arg::num_elements<>{2}, arg::num_bins<>{2},
                    arg::max_per_bin<u16>{65535}, bsource,
                    capture_output<all_output_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    // Scan 0
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 0})});
    REQUIRE(out.check(emitted_as::always_lvalue,
                      histogram_array_progress_event<>{
                          2, test_bucket<u16>({3, 1, 42, 42})}));
    in.handle(bin_increment_cluster_event<>{});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{4, test_bucket<u16>({3, 1, 0, 0})}));
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_event<>{test_bucket<u16>({3, 1, 0, 0})}));

    // Scan 1
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 1})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{2, test_bucket<u16>({5, 3, 0, 0})}));

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 0, 0, 1})});
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        histogram_array_progress_event<>{4, test_bucket<u16>({5, 3, 3, 1})}));
    REQUIRE(
        out.check(emitted_as::always_lvalue,
                  histogram_array_event<>{test_bucket<u16>({5, 3, 3, 1})}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG(
    "scan_histograms reset by event", "", ((hp P), P), hp::error_on_overflow,
    hp::stop_on_overflow, hp::saturate_on_overflow, hp::reset_on_overflow,
    hp::error_on_overflow | hp::emit_concluding_events,
    hp::stop_on_overflow | hp::emit_concluding_events,
    // saturate_on_overflow doesn't support emit_concluding_events
    hp::reset_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         scan_histograms<P, reset_event>(
                             arg::num_elements<>{2}, arg::num_bins<>{2},
                             arg::max_per_bin<u16>{65535}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    SECTION("reset before scan 0") {
        in.handle(reset_event{});
        if constexpr (emit_concluding) {
            REQUIRE(out.check(emitted_as::always_rvalue,
                              concluding_histogram_array_event<>{
                                  test_bucket<u16>({0, 0, 0, 0})}));
        }
        REQUIRE(out.check(reset_event{}));
        CHECK(bsource->bucket_count() == 1);

        in.handle(
            bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 0})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({3, 1, 0, 0})}));
        CHECK(bsource->bucket_count() == 2);
    }

    SECTION("feed scan 0, element 0") {
        in.handle(
            bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 0})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({3, 1, 0, 0})}));
        CHECK(bsource->bucket_count() == 1);

        SECTION("reset") {
            in.handle(reset_event{});
            if constexpr (emit_concluding) {
                REQUIRE(out.check(emitted_as::always_rvalue,
                                  concluding_histogram_array_event<>{
                                      test_bucket<u16>({0, 0, 0, 0})}));
            }
            REQUIRE(out.check(reset_event{}));
            CHECK(bsource->bucket_count() == 1);

            in.handle(
                bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 0, 0})});
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_array_progress_event<>{
                                  2, test_bucket<u16>({3, 1, 0, 0})}));
            CHECK(bsource->bucket_count() == 2);
        }

        SECTION("feed scan 0, element 1 (last element)") {
            in.handle(
                bin_increment_cluster_event<>{test_bucket<u16>({1, 1, 0})});
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_array_progress_event<>{
                                  4, test_bucket<u16>({3, 1, 1, 2})}));
            REQUIRE(out.check(
                emitted_as::always_lvalue,
                histogram_array_event<>{test_bucket<u16>({3, 1, 1, 2})}));

            SECTION("reset") {
                in.handle(reset_event{});
                if constexpr (emit_concluding) {
                    REQUIRE(out.check(emitted_as::always_rvalue,
                                      concluding_histogram_array_event<>{
                                          test_bucket<u16>({3, 1, 1, 2})}));
                }
                REQUIRE(out.check(reset_event{}));
                CHECK(bsource->bucket_count() == 1);

                in.handle(bin_increment_cluster_event<>{
                    test_bucket<u16>({1, 1, 0})});
                REQUIRE(out.check(emitted_as::always_lvalue,
                                  histogram_array_progress_event<>{
                                      2, test_bucket<u16>({1, 2, 0, 0})}));
                CHECK(bsource->bucket_count() == 2);
            }

            SECTION("feed scan 1, element 0") {
                in.handle(bin_increment_cluster_event<>{
                    test_bucket<u16>({0, 1, 0, 1})});
                REQUIRE(out.check(emitted_as::always_lvalue,
                                  histogram_array_progress_event<>{
                                      2, test_bucket<u16>({5, 3, 1, 2})}));

                SECTION("reset") {
                    in.handle(reset_event{});
                    if constexpr (emit_concluding) {
                        REQUIRE(
                            out.check(emitted_as::always_rvalue,
                                      concluding_histogram_array_event<>{
                                          test_bucket<u16>({3, 1, 1, 2})}));
                    }
                    REQUIRE(out.check(reset_event{}));
                    CHECK(bsource->bucket_count() == 1);

                    in.handle(bin_increment_cluster_event<>{
                        test_bucket<u16>({1, 1, 0})});
                    REQUIRE(out.check(emitted_as::always_lvalue,
                                      histogram_array_progress_event<>{
                                          2, test_bucket<u16>({1, 2, 0, 0})}));
                    CHECK(bsource->bucket_count() == 2);
                }

                SECTION("feed scan 1, element 1 (last element)") {
                    in.handle(bin_increment_cluster_event<>{
                        test_bucket<u16>({0, 0, 0, 1})});
                    REQUIRE(out.check(emitted_as::always_lvalue,
                                      histogram_array_progress_event<>{
                                          4, test_bucket<u16>({5, 3, 4, 3})}));
                    REQUIRE(out.check(emitted_as::always_lvalue,
                                      histogram_array_event<>{
                                          test_bucket<u16>({5, 3, 4, 3})}));

                    SECTION("reset") {
                        in.handle(reset_event{});
                        if constexpr (emit_concluding) {
                            REQUIRE(out.check(
                                emitted_as::always_rvalue,
                                concluding_histogram_array_event<>{
                                    test_bucket<u16>({5, 3, 4, 3})}));
                        }
                        REQUIRE(out.check(reset_event{}));
                        CHECK(bsource->bucket_count() == 1);
                    }
                }
            }
        }
    }

    in.flush();
    REQUIRE(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG("scan_histograms error_on_overflow", "", ((hp P), P),
                       hp::error_on_overflow,
                       hp::error_on_overflow | hp::emit_concluding_events) {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         scan_histograms<P, reset_event>(
                             arg::num_elements<>{2}, arg::num_bins<>{2},
                             arg::max_per_bin<u16>{3}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    SECTION("no overflow up to max_per_bin") {
        in.handle(bin_increment_cluster_event<>{
            test_bucket<u16>({0, 0, 0, 1, 1, 1})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({3, 3, 0, 0})}));

        in.flush();
        CHECK(out.check_flushed());
    }

    SECTION("throws on overflow") {
        REQUIRE_THROWS_AS(in.handle(bin_increment_cluster_event<>{
                              test_bucket<u16>({0, 1, 0, 1, 0, 1, 0})}),
                          histogram_overflow_error);
        CHECK(out.check_not_flushed());
    }
}

TEMPLATE_TEST_CASE_SIG("scan_histograms stop_on_overflow", "", ((hp P), P),
                       hp::stop_on_overflow,
                       hp::stop_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         scan_histograms<P, reset_event>(
                             arg::num_elements<>{2}, arg::num_bins<>{2},
                             arg::max_per_bin<u16>{3}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    SECTION("overflow during scan 0") {
        REQUIRE_THROWS_AS(in.handle(bin_increment_cluster_event<>{
                              test_bucket<u16>({0, 1, 0, 1, 0, 1, 0})}),
                          end_of_processing);
        if constexpr (emit_concluding) {
            REQUIRE(out.check(emitted_as::always_rvalue,
                              concluding_histogram_array_event<>{
                                  test_bucket<u16>({0, 0, 0, 0})}));
        }
        CHECK(bsource->bucket_count() == 1);
        CHECK(out.check_flushed());
    }

    SECTION("overflow during scan 1") {
        in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({1, 1, 0, 0})}));
        in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 1})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              4, test_bucket<u16>({1, 1, 1, 1})}));
        REQUIRE(out.check(
            emitted_as::always_lvalue,
            histogram_array_event<>{test_bucket<u16>({1, 1, 1, 1})}));

        REQUIRE_THROWS_AS(in.handle(bin_increment_cluster_event<>{
                              test_bucket<u16>({0, 1, 0, 1, 0})}),
                          end_of_processing);
        if constexpr (emit_concluding) {
            REQUIRE(out.check(emitted_as::always_rvalue,
                              concluding_histogram_array_event<>{
                                  test_bucket<u16>({1, 1, 1, 1})}));
        }
        CHECK(bsource->bucket_count() == 1);
        CHECK(out.check_flushed());
    }
}

TEST_CASE("scan_histograms saturate_on_overflow") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(
        valcat, scan_histograms<hp::saturate_on_overflow, reset_event>(
                    arg::num_elements<>{2}, arg::num_bins<>{2},
                    arg::max_per_bin<u16>{3}, bsource,
                    capture_output<all_output_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    SECTION("saturate during scan 0") {
        // Make sure the rest of the cluster is not lost after saturation.
        in.handle(bin_increment_cluster_event<>{
            test_bucket<u16>({0, 0, 0, 0, 0, 1, 1, 1, 1})});
        REQUIRE(out.check(warning_event{"histogram array bin saturated"}));
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({3, 3, 0, 0})}));

        SECTION("end") {}

        SECTION("further saturating cluster during scan") {
            in.handle(bin_increment_cluster_event<>{
                test_bucket<u16>({0, 0, 1, 1, 1, 1})});
            // No more warning until reset
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_array_progress_event<>{
                                  4, test_bucket<u16>({3, 3, 2, 3})}));
            REQUIRE(out.check(
                emitted_as::always_lvalue,
                histogram_array_event<>{test_bucket<u16>({3, 3, 2, 3})}));

            SECTION("end") {}

            SECTION("further saturating cluster in new scan but same round") {
                in.handle(
                    bin_increment_cluster_event<>{test_bucket<u16>({0})});
                // No more warning until reset
                REQUIRE(out.check(emitted_as::always_lvalue,
                                  histogram_array_progress_event<>{
                                      2, test_bucket<u16>({3, 3, 2, 3})}));

                SECTION("end") {}

                SECTION("saturating cluster after reset") {
                    in.handle(reset_event{});
                    REQUIRE(out.check(reset_event{}));
                    in.handle(bin_increment_cluster_event<>{
                        test_bucket<u16>({0, 0, 1, 1, 1, 1})});
                    REQUIRE(out.check(
                        warning_event{"histogram array bin saturated"}));
                    REQUIRE(out.check(emitted_as::always_lvalue,
                                      histogram_array_progress_event<>{
                                          2, test_bucket<u16>({2, 3, 0, 0})}));
                }
            }
        }
    }

    in.flush();
    CHECK(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG("scan_histograms reset_on_overflow", "", ((hp P), P),
                       hp::reset_on_overflow,
                       hp::reset_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         scan_histograms<P, reset_event>(
                             arg::num_elements<>{2}, arg::num_bins<>{2},
                             arg::max_per_bin<u16>{3}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    SECTION("overflow in scan 0, element 0 throws") {
        REQUIRE_THROWS_AS(in.handle(bin_increment_cluster_event<>{
                              test_bucket<u16>({0, 0, 0, 0})}),
                          histogram_overflow_error);
    }

    SECTION("overflow in scan 0, element 1 throws") {
        in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 0, 0})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({3, 0, 0, 0})}));

        REQUIRE_THROWS_AS(in.handle(bin_increment_cluster_event<>{
                              test_bucket<u16>({0, 0, 0, 0})}),
                          histogram_overflow_error);
    }

    SECTION("no overflow in scan 0") {
        in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 0, 0})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              2, test_bucket<u16>({3, 0, 0, 0})}));

        in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0, 0, 0})});
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_progress_event<>{
                              4, test_bucket<u16>({3, 0, 3, 0})}));
        REQUIRE(out.check(
            emitted_as::always_lvalue,
            histogram_array_event<>{test_bucket<u16>({3, 0, 3, 0})}));
        CHECK(bsource->bucket_count() == 1);

        SECTION("end") {}

        SECTION("overflow in scan 1, element 0") {
            in.handle(bin_increment_cluster_event<>{
                test_bucket<u16>({0, 0, 0, 1, 1})});
            if constexpr (emit_concluding) {
                REQUIRE(out.check(emitted_as::always_rvalue,
                                  concluding_histogram_array_event<>{
                                      test_bucket<u16>({3, 0, 3, 0})}));
            }
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_array_progress_event<>{
                                  2, test_bucket<u16>({3, 2, 0, 0})}));
            CHECK(bsource->bucket_count() == 2);
        }

        SECTION("single-cluster overflow in scan 1, element 0") {
            REQUIRE_THROWS_AS(in.handle(bin_increment_cluster_event<>{
                                  test_bucket<u16>({0, 0, 0, 0})}),
                              histogram_overflow_error);
            if constexpr (emit_concluding) {
                REQUIRE(out.check(emitted_as::always_rvalue,
                                  concluding_histogram_array_event<>{
                                      test_bucket<u16>({3, 0, 3, 0})}));
            }
            CHECK(bsource->bucket_count() == 2);
        }

        SECTION("overflow in scan 1, element 1") {
            in.handle(bin_increment_cluster_event<>{test_bucket<u16>({1, 1})});
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_array_progress_event<>{
                                  2, test_bucket<u16>({3, 2, 3, 0})}));
            CHECK(bsource->bucket_count() == 1);

            in.handle(
                bin_increment_cluster_event<>{test_bucket<u16>({0, 1, 1})});
            if constexpr (emit_concluding) {
                REQUIRE(out.check(emitted_as::always_rvalue,
                                  concluding_histogram_array_event<>{
                                      test_bucket<u16>({3, 0, 3, 0})}));
            }
            REQUIRE(out.check(emitted_as::always_lvalue,
                              histogram_array_progress_event<>{
                                  4, test_bucket<u16>({0, 2, 1, 2})}));
            REQUIRE(out.check(
                emitted_as::always_lvalue,
                histogram_array_event<>{test_bucket<u16>({0, 2, 1, 2})}));
            CHECK(bsource->bucket_count() == 2);
        }
    }

    in.flush();
    CHECK(out.check_flushed());
}

TEMPLATE_TEST_CASE_SIG(
    "scan_histograms reset_on_overflow with max_per_bin = 0", "", ((hp P), P),
    hp::reset_on_overflow,
    hp::reset_on_overflow | hp::emit_concluding_events) {
    static constexpr bool emit_concluding =
        (P & hp::emit_concluding_events) != hp::default_policy;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto bsource = test_bucket_source<u16>::create(
        new_delete_bucket_source<u16>::create(), 42);
    auto in = feed_input(valcat,
                         scan_histograms<P, reset_event>(
                             arg::num_elements<>{1}, arg::num_bins<>{1},
                             arg::max_per_bin<u16>{0}, bsource,
                             capture_output<all_output_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_output_events>(valcat, ctx, "out");

    SECTION("overflow in scan 0 throws") {
        REQUIRE_THROWS_AS(
            in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0})}),
            histogram_overflow_error);
    }

    SECTION("overflow in scan 1") {
        in.handle(bin_increment_cluster_event<>{});
        REQUIRE(out.check(
            emitted_as::always_lvalue,
            histogram_array_progress_event<>{1, test_bucket<u16>({0})}));
        REQUIRE(out.check(emitted_as::always_lvalue,
                          histogram_array_event<>{test_bucket<u16>({0})}));

        REQUIRE_THROWS_AS(
            in.handle(bin_increment_cluster_event<>{test_bucket<u16>({0})}),
            histogram_overflow_error);
        if constexpr (emit_concluding) {
            REQUIRE(out.check(
                emitted_as::always_rvalue,
                concluding_histogram_array_event<>{test_bucket<u16>({0})}));
        }
    }

    in.flush();
    CHECK(out.check_flushed());
}

} // namespace tcspc
