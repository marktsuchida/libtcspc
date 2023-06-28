/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogram_elementwise.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <array>
#include <cstdint>

#include <catch2/catch_all.hpp>

using namespace tcspc;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using start_event = timestamped_test_event<0>;
using reset_event = timestamped_test_event<2>;
using misc_event = timestamped_test_event<3>;

//
// Test cases for histogram_elementwise
//

TEMPLATE_TEST_CASE("Histogram elementwise, zero elements",
                   "[histogram_elementwise]", saturate_on_overflow,
                   error_on_overflow) {
    auto out =
        capture_output<event_set<element_histogram_event<u16>,
                                 histogram_array_event<u16>, misc_event>>();
    auto in =
        feed_input<event_set<bin_increment_batch_event<u32>, misc_event>>(
            histogram_elementwise<u32, u16, TestType>(0, 1, 1,
                                                      ref_processor(out)));
    in.require_output_checked(out);

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed_end();
    REQUIRE(out.check_end());
}

TEMPLATE_TEST_CASE("Histogram elementwise, zero bins",
                   "[histogram_elementwise]", saturate_on_overflow,
                   error_on_overflow) {
    auto out = capture_output<
        event_set<element_histogram_event<u16>, histogram_array_event<u16>>>();
    auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
        histogram_elementwise<u32, u16, TestType>(1, 0, 1,
                                                  ref_processor(out)));
    in.require_output_checked(out);

    in.feed(bin_increment_batch_event<u32>{{42, 43}, {}});
    REQUIRE(
        out.check(element_histogram_event<u16>{{42, 43}, 0, {}, {0, 0}, 0}));
    REQUIRE(out.check(histogram_array_event<u16>{{42, 43}, {}, {0, 0}, 1}));
    in.feed_end();
    REQUIRE(out.check_end());
}

TEMPLATE_TEST_CASE("Histogram elementwise, no overflow",
                   "[histogram_elementwise]", saturate_on_overflow,
                   error_on_overflow) {
    auto out = capture_output<
        event_set<element_histogram_event<u16>, histogram_array_event<u16>>>();
    auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
        histogram_elementwise<u32, u16, TestType>(2, 2, 100,
                                                  ref_processor(out)));
    in.require_output_checked(out);

    in.feed(bin_increment_batch_event<u32>{{42, 43}, {0}});
    std::vector<u16> elem_hist{1, 0};
    REQUIRE(out.check(element_histogram_event<u16>{
        {42, 43}, 0, autocopy_span(elem_hist), {1, 0}, 0}));

    in.feed(bin_increment_batch_event<u32>{{44, 45}, {0, 1}});
    elem_hist = {1, 1};
    REQUIRE(out.check(element_histogram_event<u16>{
        {44, 45}, 1, autocopy_span(elem_hist), {3, 0}, 0}));
    std::vector<u16> hist_arr{1, 0, 1, 1};
    REQUIRE(out.check(histogram_array_event<u16>{
        {42, 45}, autocopy_span(hist_arr), {3, 0}, 1}));

    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("Histogram elementwise, saturate on overflow",
          "[histogram_elementwise]") {
    auto out = capture_output<
        event_set<element_histogram_event<u16>, histogram_array_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_elementwise<u32, u16, saturate_on_overflow>(
                1, 1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{{42, 43}, {0}}); // Overflow
        std::vector<u16> elem_hist{0};
        REQUIRE(out.check(element_histogram_event<u16>{
            {42, 43}, 0, autocopy_span(elem_hist), {1, 1}, 0}));
        std::vector<u16> hist_arr{0};
        REQUIRE(out.check(histogram_array_event<u16>{
            {42, 43}, autocopy_span(hist_arr), {1, 1}, 1}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_elementwise<u32, u16, saturate_on_overflow>(
                1, 1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{{42, 43}, {0, 0}}); // Overflow
        std::vector<u16> elem_hist{1};
        REQUIRE(out.check(element_histogram_event<u16>{
            {42, 43}, 0, autocopy_span(elem_hist), {2, 1}, 0}));
        std::vector<u16> hist_arr{1};
        REQUIRE(out.check(histogram_array_event<u16>{
            {42, 43}, autocopy_span(hist_arr), {2, 1}, 1}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Histogram elementwise, error on overflow",
          "[histogram_elementwise]") {
    auto out = capture_output<
        event_set<element_histogram_event<u16>, histogram_array_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_elementwise<u32, u16, error_on_overflow>(
                1, 1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{{42, 43}, {0}}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_elementwise<u32, u16, error_on_overflow>(
                1, 1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{{42, 43}, {0, 0}}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }
}

//
// Test cases for histogram_elementwise_accumulate
//

// These are written in a newer style (per operation rather than per scenario)
// than above tests for histogram_elementwise (which should be updated).

namespace {

using hea_input_events =
    event_set<bin_increment_batch_event<u8>, reset_event, misc_event>;
using hea_output_events =
    event_set<element_histogram_event<u8>, histogram_array_event<u8>,
              concluding_histogram_array_event<u8>, misc_event>;

using hea_output_events_no_concluding =
    event_set<element_histogram_event<u8>, histogram_array_event<u8>,
              misc_event>;

} // namespace

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate without emit-concluding yields empty stream from empty stream",
    "[histogram_elementwise_accumulate]", saturate_on_overflow,
    reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto num_elements = GENERATE(std::size_t{0}, 1, 3);
    auto num_bins = GENERATE(std::size_t{0}, 1, 4);
    auto out = capture_output<hea_output_events_no_concluding>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event, TestType, false>(
            num_elements, num_bins, 10, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("empty stream") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("unrelated event only") {
        in.feed(misc_event{});
        REQUIRE(out.check(misc_event{}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate without emit-concluding finishes correctly",
    "[histogram_elementwise_accumulate]", saturate_on_overflow,
    reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto out = capture_output<hea_output_events_no_concluding>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event, TestType, false>(
            2, 3, 255, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("end before cycle 0") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("feed cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0}});
        elem_hist = {1, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {1, 0}, 0}));

        SECTION("end mid cycle 0") {
            in.feed_end();
            REQUIRE(out.check_end());
        }

        SECTION("feed cycle 0, element 1") {
            in.feed(bin_increment_batch_event<u8>{{44, 45}, {1}});
            elem_hist = {0, 1, 0};
            REQUIRE(out.check(element_histogram_event<u8>{
                {44, 45}, 1, autocopy_span(elem_hist), {2, 0}, 0}));
            hist_arr = {1, 0, 0, 0, 1, 0};
            REQUIRE(out.check(histogram_array_event<u8>{
                {42, 45}, autocopy_span(hist_arr), {2, 0}, 1}));

            SECTION("end after cycle 0") {
                in.feed_end();
                REQUIRE(out.check_end());
            }

            SECTION("feed cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {2}});
                elem_hist = {1, 0, 1};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {3, 0}, 1}));

                SECTION("end mid cycle 1") {
                    in.feed_end();
                    REQUIRE(out.check_end());
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate with emit-concluding emits concluding event when ended",
    "[histogram_elementwise_accumulate]", reset_on_overflow, stop_on_overflow,
    error_on_overflow) {
    auto out = capture_output<hea_output_events>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event, TestType, true>(
            2, 3, 255, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("end before cycle 0") {
        in.feed_end();
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(concluding_histogram_array_event<u8>{
            {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
        REQUIRE(out.check_end());
    }

    SECTION("feed cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0}});
        elem_hist = {1, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {1, 0}, 0}));

        SECTION("end mid cycle 0") {
            in.feed_end();
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(concluding_histogram_array_event<u8>{
                {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
            REQUIRE(out.check_end());
        }

        SECTION("feed cycle 0, element 1") {
            in.feed(bin_increment_batch_event<u8>{{44, 45}, {1}});
            elem_hist = {0, 1, 0};
            REQUIRE(out.check(element_histogram_event<u8>{
                {44, 45}, 1, autocopy_span(elem_hist), {2, 0}, 0}));
            hist_arr = {1, 0, 0, 0, 1, 0};
            REQUIRE(out.check(histogram_array_event<u8>{
                {42, 45}, autocopy_span(hist_arr), {2, 0}, 1}));

            SECTION("end after cycle 0") {
                in.feed_end();
                hist_arr = {1, 0, 0, 0, 1, 0};
                REQUIRE(out.check(concluding_histogram_array_event<u8>{
                    {42, 45}, autocopy_span(hist_arr), {2, 0}, 1, true}));
                REQUIRE(out.check_end());
            }

            SECTION("feed cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {2}});
                elem_hist = {1, 0, 1};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {3, 0}, 1}));

                SECTION("end mid cycle 1") {
                    in.feed_end();
                    hist_arr = {1, 0, 0, 0, 1, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<u8>{
                        {42, 45}, autocopy_span(hist_arr), {2, 0}, 1, true}));
                    REQUIRE(out.check_end());
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate with emit-concluding emits concluding event when reset",
    "[histogram_elementwise_accumulate]", reset_on_overflow, stop_on_overflow,
    error_on_overflow) {
    auto out = capture_output<hea_output_events>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event, TestType, true>(
            2, 3, 255, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("reset before cycle 0") {
        in.feed(reset_event{});
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(concluding_histogram_array_event<u8>{
            {}, autocopy_span(hist_arr), {0, 0}, 0, false}));
        in.feed_end();
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(concluding_histogram_array_event<u8>{
            {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
        REQUIRE(out.check_end());
    }

    SECTION("feed cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0}});
        elem_hist = {1, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {1, 0}, 0}));

        SECTION("reset mid cycle 0") {
            in.feed(reset_event{});
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(concluding_histogram_array_event<u8>{
                {}, autocopy_span(hist_arr), {0, 0}, 0, false}));
            in.feed_end();
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(concluding_histogram_array_event<u8>{
                {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
            REQUIRE(out.check_end());
        }

        SECTION("feed cycle 0, element 1") {
            in.feed(bin_increment_batch_event<u8>{{44, 45}, {1}});
            elem_hist = {0, 1, 0};
            REQUIRE(out.check(element_histogram_event<u8>{
                {44, 45}, 1, autocopy_span(elem_hist), {2, 0}, 0}));
            hist_arr = {1, 0, 0, 0, 1, 0};
            REQUIRE(out.check(histogram_array_event<u8>{
                {42, 45}, autocopy_span(hist_arr), {2, 0}, 1}));

            SECTION("reset after cycle 0") {
                in.feed(reset_event{});
                hist_arr = {1, 0, 0, 0, 1, 0};
                REQUIRE(out.check(concluding_histogram_array_event<u8>{
                    {42, 45}, autocopy_span(hist_arr), {2, 0}, 1, false}));
                in.feed_end();
                hist_arr = {0, 0, 0, 0, 0, 0};
                REQUIRE(out.check(concluding_histogram_array_event<u8>{
                    {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
                REQUIRE(out.check_end());
            }

            SECTION("feed cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {2}});
                elem_hist = {1, 0, 1};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {3, 0}, 1}));

                SECTION("reset mid cycle 1") {
                    in.feed(reset_event{});
                    hist_arr = {1, 0, 0, 0, 1, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<u8>{
                        {42, 45}, autocopy_span(hist_arr), {2, 0}, 1, false}));
                    in.feed_end();
                    hist_arr = {0, 0, 0, 0, 0, 0};
                    REQUIRE(out.check(concluding_histogram_array_event<u8>{
                        {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
                    REQUIRE(out.check_end());
                }
            }
        }
    }
}

TEST_CASE("histogram_elementwise_accumulate with saturate-on-overflow",
          "[histogram_elementwise_accumulate]") {
    auto out = capture_output<hea_output_events_no_concluding>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event,
                                         saturate_on_overflow, false>(
            2, 3, 4, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;

    SECTION("overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0, 0, 0, 0, 0}});
        elem_hist = {4, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {6, 2}, 0}));

        SECTION("end") {
            in.feed_end();
            REQUIRE(out.check_end());
        }

        SECTION("reset") {
            in.feed(reset_event{});

            SECTION("saturated count zeroed after reset") {
                in.feed(bin_increment_batch_event<u8>{{44, 45}, {}});
                elem_hist = {0, 0, 0};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {44, 45}, 0, autocopy_span(elem_hist), {0, 0}, 0}));
                in.feed_end();
                REQUIRE(out.check_end());
            }
        }
    }
}

TEST_CASE("histogram_elementwise_accumulate with reset-on-overflow",
          "[histogram_elementwise_accumulate]") {
    auto out = capture_output<hea_output_events>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event,
                                         reset_on_overflow, true>(
            2, 3, 4, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("single-batch overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0, 0, 0, 0, 0}});
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {2, 0}, 0}));

        SECTION("single-batch overflow during cycle 0, element 1") {
            in.feed(
                bin_increment_batch_event<u8>{{44, 45}, {1, 1, 1, 1, 1, 1}});
            REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<u8>{{44, 45}, {1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(element_histogram_event<u8>{
                {44, 45}, 1, autocopy_span(elem_hist), {4, 0}, 0}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(out.check(histogram_array_event<u8>{
                {42, 45}, autocopy_span(hist_arr), {4, 0}, 1}));

            SECTION("overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0, 0, 0}});
                hist_arr = {2, 0, 0, 0, 2, 0};
                REQUIRE(out.check(concluding_histogram_array_event<u8>{
                    {42, 45}, autocopy_span(hist_arr), {4, 0}, 1, false}));
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {3, 0}, 0}));

                in.feed_end();
                hist_arr = {0, 0, 0, 0, 0, 0};
                REQUIRE(out.check(concluding_histogram_array_event<u8>{
                    {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
            }

            SECTION("single-batch overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47},
                                                      {0, 0, 0, 0, 0, 0}});
                hist_arr = {2, 0, 0, 0, 2, 0};
                REQUIRE(out.check(concluding_histogram_array_event<u8>{
                    {42, 45}, autocopy_span(hist_arr), {4, 0}, 1, false}));
                REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {5, 0}, 1}));

                SECTION("overflow during cycle 1, element 1") {
                    in.feed(
                        bin_increment_batch_event<u8>{{48, 49}, {1, 1, 1}});
                    hist_arr = {2, 0, 0, 0, 2, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<u8>{
                        {42, 45}, autocopy_span(hist_arr), {4, 0}, 1, false}));
                    elem_hist = {0, 3, 0};
                    REQUIRE(out.check(element_histogram_event<u8>{
                        {48, 49}, 1, autocopy_span(elem_hist), {4, 0}, 0}));
                    hist_arr = {1, 0, 0, 0, 3, 0};
                    REQUIRE(out.check(histogram_array_event<u8>{
                        {46, 49}, autocopy_span(hist_arr), {4, 0}, 1}));

                    in.feed_end();
                    hist_arr = {1, 0, 0, 0, 3, 0};
                    REQUIRE(out.check(concluding_histogram_array_event<u8>{
                        {46, 49}, autocopy_span(hist_arr), {4, 0}, 1, true}));
                }

                SECTION("single-batch overflow during cycle 1, element 1") {
                    in.feed(bin_increment_batch_event<u8>{{48, 49},
                                                          {1, 1, 1, 1, 1, 1}});
                    hist_arr = {2, 0, 0, 0, 2, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<u8>{
                        {42, 45}, autocopy_span(hist_arr), {4, 0}, 1, false}));
                    REQUIRE_THROWS_AS(out.check_end(),
                                      histogram_overflow_error);
                }
            }
        }
    }
}

TEST_CASE("histogram_elementwise_accumulate with stop-on-overflow",
          "[histogram_elementwise_accumulate]") {
    auto out = capture_output<hea_output_events>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event, stop_on_overflow,
                                         true>(2, 3, 4, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0, 0, 0, 0}});
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(concluding_histogram_array_event<u8>{
            {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
        REQUIRE(out.check_end());
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {2, 0}, 0}));

        SECTION("overflow during cycle 0, element 1") {
            in.feed(
                bin_increment_batch_event<u8>{{44, 45}, {1, 1, 1, 1, 1, 1}});
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(concluding_histogram_array_event<u8>{
                {}, autocopy_span(hist_arr), {0, 0}, 0, true}));
            REQUIRE(out.check_end());
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<u8>{{44, 45}, {1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(element_histogram_event<u8>{
                {44, 45}, 1, autocopy_span(elem_hist), {4, 0}, 0}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(out.check(histogram_array_event<u8>{
                {42, 45}, autocopy_span(hist_arr), {4, 0}, 1}));

            SECTION("overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0, 0, 0}});
                hist_arr = {2, 0, 0, 0, 2, 0};
                REQUIRE(out.check(concluding_histogram_array_event<u8>{
                    {42, 45}, autocopy_span(hist_arr), {4, 0}, 1, true}));
                REQUIRE(out.check_end());
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {5, 0}, 1}));

                SECTION("overflow during cycle 1, element 1") {
                    in.feed(
                        bin_increment_batch_event<u8>{{48, 49}, {1, 1, 1}});
                    hist_arr = {2, 0, 0, 0, 2, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<u8>{
                        {42, 45}, autocopy_span(hist_arr), {4, 0}, 1, true}));
                    REQUIRE(out.check_end());
                }
            }
        }
    }
}

TEST_CASE(
    "histogram_elementwise_accumulate with error-on-overflow, emit-concluding",
    "[histogram_elementwise_accumulate]") {
    auto out = capture_output<hea_output_events>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event,
                                         error_on_overflow, true>(
            2, 3, 4, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0, 0, 0, 0}});
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {2, 0}, 0}));

        SECTION("overflow during cycle 0, element 1") {
            in.feed(
                bin_increment_batch_event<u8>{{44, 45}, {1, 1, 1, 1, 1, 1}});
            REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<u8>{{44, 45}, {1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(element_histogram_event<u8>{
                {44, 45}, 1, autocopy_span(elem_hist), {4, 0}, 0}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(out.check(histogram_array_event<u8>{
                {42, 45}, autocopy_span(hist_arr), {4, 0}, 1}));

            SECTION("overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0, 0, 0}});
                REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {5, 0}, 1}));

                SECTION("overflow during cycle 1, element 1") {
                    in.feed(
                        bin_increment_batch_event<u8>{{48, 49}, {1, 1, 1}});
                    REQUIRE_THROWS_AS(out.check_end(),
                                      histogram_overflow_error);
                }
            }
        }
    }
}

TEST_CASE(
    "histogram_elementwise_accumulate with error-on-overflow, no emit-concluding",
    "[histogram_elementwise_accumulate]") {
    auto out = capture_output<hea_output_events_no_concluding>();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<u8, u8, reset_event,
                                         error_on_overflow, false>(
            2, 3, 4, ref_processor(out)));
    in.require_output_checked(out);

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0, 0, 0, 0}});
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<u8>{{42, 43}, {0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(out.check(element_histogram_event<u8>{
            {42, 43}, 0, autocopy_span(elem_hist), {2, 0}, 0}));

        SECTION("overflow during cycle 0, element 1") {
            in.feed(
                bin_increment_batch_event<u8>{{44, 45}, {1, 1, 1, 1, 1, 1}});
            REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<u8>{{44, 45}, {1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(element_histogram_event<u8>{
                {44, 45}, 1, autocopy_span(elem_hist), {4, 0}, 0}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(out.check(histogram_array_event<u8>{
                {42, 45}, autocopy_span(hist_arr), {4, 0}, 1}));

            SECTION("overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0, 0, 0}});
                REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<u8>{{46, 47}, {0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(element_histogram_event<u8>{
                    {46, 47}, 0, autocopy_span(elem_hist), {5, 0}, 1}));

                SECTION("overflow during cycle 1, element 1") {
                    in.feed(
                        bin_increment_batch_event<u8>{{48, 49}, {1, 1, 1}});
                    REQUIRE_THROWS_AS(out.check_end(),
                                      histogram_overflow_error);
                }
            }
        }
    }
}
