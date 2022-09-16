/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/histogram.hpp"

#include "flimevt/event_set.hpp"
#include "flimevt/ref_processor.hpp"
#include "flimevt/test_utils.hpp"

#include <cstdint>

#include <catch2/catch.hpp>

using namespace flimevt;

using u16 = std::uint16_t;
using u32 = std::uint32_t;

using reset_event = timestamped_test_event<0>;
using misc_event = timestamped_test_event<1>;

TEMPLATE_TEST_CASE("Histogram, zero bins", "[histogram]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto out = capture_output<event_set<
        histogram_event<u16>, concluding_histogram_event<u16>, misc_event>>();
    auto in = feed_input<
        event_set<bin_increment_event<u32>, reset_event, misc_event>>(
        histogram<u32, u16, reset_event, TestType>(0, 0, ref_processor(out)));
    in.require_output_checked(out);

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed(reset_event{});
    REQUIRE(out.check(
        concluding_histogram_event<u16>{0, 0, {}, 0, 0, false, false}));
    in.feed_end();
    REQUIRE(out.check(
        concluding_histogram_event<u16>{0, 0, {}, 0, 0, false, true}));
    REQUIRE(out.check_end());
}

TEMPLATE_TEST_CASE("Histogram, no overflow", "[histogram]",
                   saturate_on_overflow, reset_on_overflow, stop_on_overflow,
                   error_on_overflow) {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();
    auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
        histogram<u32, u16, reset_event, TestType>(2, 100,
                                                   ref_processor(out)));
    in.require_output_checked(out);

    in.feed(bin_increment_event<u32>{42, 0});
    REQUIRE(out.check(histogram_event<u16>{42, 42, {1, 0}, 1, 0}));
    in.feed(bin_increment_event<u32>{43, 1});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {1, 1}, 2, 0}));
    in.feed(reset_event{44});
    REQUIRE(out.check(
        concluding_histogram_event<u16>{42, 43, {1, 1}, 2, 0, true, false}));
    in.feed(bin_increment_event<u32>{45, 0});
    REQUIRE(out.check(histogram_event<u16>{45, 45, {1, 0}, 1, 0}));
    in.feed_end();
    REQUIRE(out.check(
        concluding_histogram_event<u16>{45, 45, {1, 0}, 1, 0, true, true}));
    REQUIRE(out.check_end());
}

TEST_CASE("Histogram, saturate on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, saturate_on_overflow>(
                1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0}); // Overflow
        REQUIRE(out.check(histogram_event<u16>{42, 42, {0}, 1, 1}));
        in.feed_end();
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 42, {0}, 1, 1, true, true}));
        REQUIRE(out.check_end());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, saturate_on_overflow>(
                1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0});
        REQUIRE(out.check(histogram_event<u16>{42, 42, {1}, 1, 0}));
        in.feed(bin_increment_event<u32>{43, 0}); // Overflow
        REQUIRE(out.check(histogram_event<u16>{42, 43, {1}, 2, 1}));
        in.feed(reset_event{44});
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 43, {1}, 2, 1, true, false}));
        in.feed(bin_increment_event<u32>{45, 0});
        REQUIRE(out.check(histogram_event<u16>{45, 45, {1}, 1, 0}));
        in.feed_end();
        REQUIRE(out.check(
            concluding_histogram_event<u16>{45, 45, {1}, 1, 0, true, true}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Histogram, reset on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, reset_on_overflow>(
                1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, reset_on_overflow>(
                1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0});
        REQUIRE(out.check(histogram_event<u16>{42, 42, {1}, 1, 0}));
        in.feed(bin_increment_event<u32>{43, 0}); // Overflow
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 42, {1}, 1, 0, true, false}));
        REQUIRE(out.check(histogram_event<u16>{43, 43, {1}, 1, 0}));
        in.feed_end();
        REQUIRE(out.check(
            concluding_histogram_event<u16>{43, 43, {1}, 1, 0, true, true}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Histogram, stop on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, stop_on_overflow>(
                1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0}); // Overflow
        REQUIRE(out.check(
            concluding_histogram_event<u16>{0, 0, {0}, 0, 0, false, true}));
        REQUIRE(out.check_end());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, stop_on_overflow>(
                1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0});
        REQUIRE(out.check(histogram_event<u16>{42, 42, {1}, 1, 0}));
        in.feed(bin_increment_event<u32>{43, 0}); // Overflow
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 42, {1}, 1, 0, true, true}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Histogram, error on overflow", "[histogram]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, error_on_overflow>(
                1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_event<u32>, reset_event>>(
            histogram<u32, u16, reset_event, error_on_overflow>(
                1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_event<u32>{42, 0});
        REQUIRE(out.check(histogram_event<u16>{42, 42, {1}, 1, 0}));
        in.feed(bin_increment_event<u32>{43, 0}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }
}

TEMPLATE_TEST_CASE("Histogram in batches, zero bins", "[histogram_in_batches]",
                   saturate_on_overflow, error_on_overflow) {
    auto out = capture_output<event_set<
        histogram_event<u16>, concluding_histogram_event<u16>, misc_event>>();
    auto in =
        feed_input<event_set<bin_increment_batch_event<u32>, misc_event>>(
            histogram_in_batches<u32, u16, TestType>(0, 0,
                                                     ref_processor(out)));
    in.require_output_checked(out);

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed(bin_increment_batch_event<u32>{42, 43, {}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {}, 0, 0}));
    in.feed_end();
    REQUIRE(out.check_end());
}

TEMPLATE_TEST_CASE("Histogram in batches, no overflow",
                   "[histogram_in_batches]", saturate_on_overflow,
                   error_on_overflow) {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();
    auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
        histogram_in_batches<u32, u16, TestType>(2, 100, ref_processor(out)));
    in.require_output_checked(out);

    in.feed(bin_increment_batch_event<u32>{42, 43, {0}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {1, 0}, 1, 0}));
    in.feed(bin_increment_batch_event<u32>{42, 43, {0, 1}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {1, 1}, 2, 0}));
    in.feed(bin_increment_batch_event<u32>{42, 43, {1, 0}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {1, 1}, 2, 0}));
    in.feed(bin_increment_batch_event<u32>{42, 43, {1, 1}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {0, 2}, 2, 0}));
    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("Histogram in batches, saturate on overflow",
          "[histogram_in_batches]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_in_batches<u32, u16, saturate_on_overflow>(
                1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}}); // Overflow
        REQUIRE(out.check(histogram_event<u16>{42, 43, {0}, 1, 1}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_in_batches<u32, u16, saturate_on_overflow>(
                1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0, 0}}); // Overflow
        REQUIRE(out.check(histogram_event<u16>{42, 43, {1}, 2, 1}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Histogram in batches, error on overflow", "[histogram_in_batches") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_in_batches<u32, u16, error_on_overflow>(
                1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<event_set<bin_increment_batch_event<u32>>>(
            histogram_in_batches<u32, u16, error_on_overflow>(
                1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0, 0}}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }
}

TEMPLATE_TEST_CASE("Accumulate histograms, zero bins",
                   "[accumulate_histograms]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto out = capture_output<event_set<
        histogram_event<u16>, concluding_histogram_event<u16>, misc_event>>();
    auto in = feed_input<
        event_set<bin_increment_batch_event<u32>, reset_event, misc_event>>(
        accumulate_histograms<u32, u16, reset_event, TestType>(
            0, 0, ref_processor(out)));
    in.require_output_checked(out);

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed(reset_event{});
    REQUIRE(out.check(
        concluding_histogram_event<u16>{0, 0, {}, 0, 0, false, false}));
    in.feed(bin_increment_batch_event<u32>{42, 43, {}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {}, 0, 0}));
    in.feed(reset_event{});
    REQUIRE(out.check(
        concluding_histogram_event<u16>{42, 43, {}, 0, 0, true, false}));
    in.feed(bin_increment_batch_event<u32>{42, 43, {}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {}, 0, 0}));
    in.feed_end();
    REQUIRE(out.check(
        concluding_histogram_event<u16>{42, 43, {}, 0, 0, true, true}));
    REQUIRE(out.check_end());
}

TEMPLATE_TEST_CASE("Accumulate histograms, no overflow",
                   "[accumulate_histograms]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();
    auto in =
        feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
            accumulate_histograms<u32, u16, reset_event, TestType>(
                2, 100, ref_processor(out)));
    in.require_output_checked(out);

    in.feed(bin_increment_batch_event<u32>{42, 43, {0}});
    REQUIRE(out.check(histogram_event<u16>{42, 43, {1, 0}, 1, 0}));
    in.feed(bin_increment_batch_event<u32>{44, 45, {0, 1}});
    REQUIRE(out.check(histogram_event<u16>{42, 45, {2, 1}, 3, 0}));
    in.feed(reset_event{46});
    REQUIRE(out.check(
        concluding_histogram_event<u16>{42, 45, {2, 1}, 3, 0, true, false}));
    in.feed(bin_increment_batch_event<u32>{47, 48, {1}});
    REQUIRE(out.check(histogram_event<u16>{47, 48, {0, 1}, 1, 0}));
    in.feed_end();
    REQUIRE(out.check(
        concluding_histogram_event<u16>{47, 48, {0, 1}, 1, 0, true, true}));
    REQUIRE(out.check_end());
}

TEST_CASE("Accumulate histograms, saturate on overflow",
          "[accumulate_histograms]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event,
                                      saturate_on_overflow>(
                    1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}}); // Overflow
        REQUIRE(out.check(histogram_event<u16>{42, 43, {0}, 1, 1}));
        in.feed_end();
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 43, {0}, 1, 1, true, true}));
        REQUIRE(out.check_end());
    }

    SECTION("Max per bin = 1") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event,
                                      saturate_on_overflow>(
                    1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}});
        REQUIRE(out.check(histogram_event<u16>{42, 43, {1}, 1, 0}));
        in.feed(bin_increment_batch_event<u32>{44, 45, {0}}); // Overflow
        REQUIRE(out.check(histogram_event<u16>{42, 45, {1}, 2, 1}));
        in.feed(reset_event{46});
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 45, {1}, 2, 1, true, false}));
        in.feed(bin_increment_batch_event<u32>{47, 48, {0}});
        REQUIRE(out.check(histogram_event<u16>{47, 48, {1}, 1, 0}));
        in.feed_end();
        REQUIRE(out.check(
            concluding_histogram_event<u16>{47, 48, {1}, 1, 0, true, true}));
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Accumulate histograms, reset on overflow",
          "[accumulate_histograms]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event,
                                      reset_on_overflow>(1, 0,
                                                         ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event,
                                      reset_on_overflow>(1, 1,
                                                         ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}});
        REQUIRE(out.check(histogram_event<u16>{42, 43, {1}, 1, 0}));
        in.feed(bin_increment_batch_event<u32>{44, 45, {0}}); // Overflow
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 43, {1}, 1, 0, true, false}));
        REQUIRE(out.check(histogram_event<u16>{44, 45, {1}, 1, 0}));

        SECTION("Normal end") {
            in.feed_end();
            REQUIRE(out.check(concluding_histogram_event<u16>{
                44, 45, {1}, 1, 0, true, true}));
            REQUIRE(out.check_end());
        }

        SECTION("Error on single-batch overflow") {
            in.feed(
                bin_increment_batch_event<u32>{46, 47, {0, 0}}); // Overflow
            // Reset-before-overflow succeeds:
            REQUIRE(out.check(concluding_histogram_event<u16>{
                44, 45, {1}, 1, 0, true, false}));
            // But the batch overflows by itself:
            REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
        }
    }

    SECTION("Roll back batch before resetting") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event,
                                      reset_on_overflow>(2, 1,
                                                         ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {1}});
        REQUIRE(out.check(histogram_event<u16>{42, 43, {0, 1}, 1, 0}));

        SECTION("Successful reset") {
            in.feed(
                bin_increment_batch_event<u32>{44, 45, {0, 1}}); // Overflow
            REQUIRE(out.check(concluding_histogram_event<u16>{
                42, 43, {0, 1}, 1, 0, true, false}));
            REQUIRE(out.check(histogram_event<u16>{44, 45, {1, 1}, 2, 0}));
            in.feed_end();
            REQUIRE(out.check(concluding_histogram_event<u16>{
                44, 45, {1, 1}, 2, 0, true, true}));
            REQUIRE(out.check_end());
        }

        SECTION("Error on single-batch overflow") {
            in.feed(
                bin_increment_batch_event<u32>{44, 45, {0, 1, 1}}); // Overflow
            REQUIRE(out.check(concluding_histogram_event<u16>{
                42, 43, {0, 1}, 1, 0, true, false}));
            REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
        }
    }
}

TEST_CASE("Accumulate histograms, stop on overflow",
          "[accumulate_histograms]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event, stop_on_overflow>(
                    1, 0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}}); // Overflow
        REQUIRE(out.check(
            concluding_histogram_event<u16>{0, 0, {0}, 0, 0, false, true}));
        REQUIRE(out.check_end());
    }

    SECTION("Max per bin = 1") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event, stop_on_overflow>(
                    1, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}});
        REQUIRE(out.check(histogram_event<u16>{42, 43, {1}, 1, 0}));
        in.feed(bin_increment_batch_event<u32>{44, 45, {0}}); // Overflow
        REQUIRE(out.check(
            concluding_histogram_event<u16>{42, 43, {1}, 1, 0, true, true}));
        REQUIRE(out.check_end());
    }

    SECTION("Roll back batch before stopping") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event, stop_on_overflow>(
                    2, 1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {1}});
        REQUIRE(out.check(histogram_event<u16>{42, 43, {0, 1}, 1, 0}));

        SECTION("Overflow of accumulated") {
            in.feed(
                bin_increment_batch_event<u32>{44, 45, {0, 1}}); // Overflow
            REQUIRE(out.check(concluding_histogram_event<u16>{
                42, 43, {0, 1}, 1, 0, true, true}));
            REQUIRE(out.check_end());
        }

        SECTION("Single-batch overflow") {
            in.feed(
                bin_increment_batch_event<u32>{44, 45, {0, 1, 1}}); // Overflow
            REQUIRE(out.check(concluding_histogram_event<u16>{
                42, 43, {0, 1}, 1, 0, true, true}));
            REQUIRE(out.check_end());
        }
    }
}

TEST_CASE("Accumulate histograms, error on overflow",
          "[accumulate_histograms]") {
    auto out = capture_output<
        event_set<histogram_event<u16>, concluding_histogram_event<u16>>>();

    SECTION("Max per bin = 0") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event,
                                      error_on_overflow>(1, 0,
                                                         ref_processor(out)));
        in.require_output_checked(out);

        in.feed(bin_increment_batch_event<u32>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto in =
            feed_input<event_set<bin_increment_batch_event<u32>, reset_event>>(
                accumulate_histograms<u32, u16, reset_event,
                                      error_on_overflow>(1, 1,
                                                         ref_processor(out)));
        in.require_output_checked(out);

        SECTION("Overflow of accumulated") {
            in.feed(bin_increment_batch_event<u32>{42, 43, {0}});
            REQUIRE(out.check(histogram_event<u16>{42, 43, {1}, 1, 0}));
            in.feed(bin_increment_batch_event<u32>{44, 45, {0}}); // Overflow
            REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
        }

        SECTION("Single-batch overflow") {
            in.feed(
                bin_increment_batch_event<u32>{44, 45, {0, 0}}); // Overflow
            REQUIRE_THROWS_AS(out.check_end(), histogram_overflow_error);
        }
    }
}
