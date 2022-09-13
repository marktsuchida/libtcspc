/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/histogram.hpp"

#include "flimevt/event_set.hpp"

#include "processor_test_fixture.hpp"
#include "test_events.hpp"

#include <catch2/catch.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

using namespace flimevt;
using namespace flimevt::test;

using reset_event = test_event<0>;
using other_event = test_event<1>;
using bin_events =
    event_set<bin_increment_event<unsigned>, reset_event, other_event>;
using batch_events =
    event_set<bin_increment_batch_event<unsigned>, reset_event, other_event>;
using batch_events_no_reset =
    event_set<bin_increment_batch_event<unsigned>, other_event>;
using hist_events =
    event_set<histogram_event<unsigned>, accumulated_histogram_event<unsigned>,
              other_event>;
using out_vec = std::vector<event_variant<hist_events>>;

template <typename Ovfl>
auto make_histogram_fixture(std::size_t n_bins, unsigned bin_max) {
    return make_processor_test_fixture<bin_events, hist_events>(
        [=](auto &&downstream) {
            return histogram<unsigned, unsigned, reset_event, Ovfl>(
                n_bins, bin_max, std::move(downstream));
        });
}

TEMPLATE_TEST_CASE("Histogram, zero bins", "[histogram]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto f = make_histogram_fixture<TestType>(0, 0);

    f.feed(other_event{42});
    REQUIRE(f.check(other_event{42}));

    f.feed(reset_event{});
    REQUIRE(f.check(
        accumulated_histogram_event<unsigned>{0, 0, {}, 0, 0, false, false}));

    f.feed_end({});
    REQUIRE(f.check(
        accumulated_histogram_event<unsigned>{0, 0, {}, 0, 0, false, true}));
    REQUIRE(f.did_end());
}

TEMPLATE_TEST_CASE("Histogram, no overflow", "[histogram]",
                   saturate_on_overflow, reset_on_overflow, stop_on_overflow,
                   error_on_overflow) {
    auto f = make_histogram_fixture<TestType>(2, 100);

    f.feed(bin_increment_event<unsigned>{42, 0});
    REQUIRE(f.check(histogram_event<unsigned>{42, 42, {1, 0}, 1, 0}));

    f.feed(bin_increment_event<unsigned>{43, 1});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1, 1}, 2, 0}));

    f.feed(reset_event{44});
    REQUIRE(f.check(accumulated_histogram_event<unsigned>{
        42, 43, {1, 1}, 2, 0, true, false}));

    f.feed(bin_increment_event<unsigned>{45, 0});
    REQUIRE(f.check(histogram_event<unsigned>{45, 45, {1, 0}, 1, 0}));

    f.feed_end({});
    REQUIRE(f.check(accumulated_histogram_event<unsigned>{
        45, 45, {1, 0}, 1, 0, true, true}));
    REQUIRE(f.did_end());
}

TEST_CASE("Histogram, saturate on overflow", "[histogram]") {
    SECTION("Max per bin = 0") {
        auto f = make_histogram_fixture<saturate_on_overflow>(1, 0);

        f.feed(bin_increment_event<unsigned>{42, 0}); // Overflow
        REQUIRE(f.check(histogram_event<unsigned>{42, 42, {0}, 1, 1}));

        f.feed_end({});
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 42, {0}, 1, 1, true, true}));
        REQUIRE(f.did_end());
    }

    SECTION("Max per bin = 1") {
        auto f = make_histogram_fixture<saturate_on_overflow>(1, 1);

        f.feed(bin_increment_event<unsigned>{42, 0});
        REQUIRE(f.check(histogram_event<unsigned>{42, 42, {1}, 1, 0}));

        f.feed(bin_increment_event<unsigned>{43, 0}); // Overflow
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1}, 2, 1}));

        f.feed(reset_event{44});
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 43, {1}, 2, 1, true, false}));

        f.feed(bin_increment_event<unsigned>{45, 0});
        REQUIRE(f.check(histogram_event<unsigned>{45, 45, {1}, 1, 0}));

        f.feed_end({});
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            45, 45, {1}, 1, 0, true, true}));
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Histogram, reset on overflow", "[histogram]") {
    SECTION("Max per bin = 0") {
        auto f = make_histogram_fixture<reset_on_overflow>(1, 0);

        f.feed(bin_increment_event<unsigned>{42, 0}); // Overflow
        REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto f = make_histogram_fixture<reset_on_overflow>(1, 1);

        f.feed(bin_increment_event<unsigned>{42, 0});
        REQUIRE(f.check(histogram_event<unsigned>{42, 42, {1}, 1, 0}));

        f.feed(bin_increment_event<unsigned>{43, 0}); // Overflow
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 42, {1}, 1, 0, true, false}));
        REQUIRE(f.check(histogram_event<unsigned>{43, 43, {1}, 1, 0}));

        f.feed_end({});
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            43, 43, {1}, 1, 0, true, true}));
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Histogram, stop on overflow", "[histogram]") {
    SECTION("Max per bin = 0") {
        auto f = make_histogram_fixture<stop_on_overflow>(1, 0);

        f.feed(bin_increment_event<unsigned>{42, 0}); // Overflow
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            0, 0, {0}, 0, 0, false, true}));
        REQUIRE(f.did_end());
    }

    SECTION("Max per bin = 1") {
        auto f = make_histogram_fixture<stop_on_overflow>(1, 1);

        f.feed(bin_increment_event<unsigned>{42, 0});
        REQUIRE(f.check(histogram_event<unsigned>{42, 42, {1}, 1, 0}));

        f.feed(bin_increment_event<unsigned>{43, 0}); // Overflow
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 42, {1}, 1, 0, true, true}));
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Histogram, error on overflow", "[histogram]") {
    SECTION("Max per bin = 0") {
        auto f = make_histogram_fixture<error_on_overflow>(1, 0);

        f.feed(bin_increment_event<unsigned>{42, 0}); // Overflow
        REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto f = make_histogram_fixture<error_on_overflow>(1, 1);

        f.feed(bin_increment_event<unsigned>{42, 0});
        REQUIRE(f.check(histogram_event<unsigned>{42, 42, {1}, 1, 0}));

        f.feed(bin_increment_event<unsigned>{43, 0}); // Overflow
        REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
    }
}

template <typename Ovfl>
auto make_histogram_in_batches_fixture(std::size_t n_bins, unsigned bin_max) {
    return make_processor_test_fixture<batch_events_no_reset, hist_events>(
        [=](auto &&downstream) {
            return histogram_in_batches<unsigned, unsigned, Ovfl>(
                n_bins, bin_max, std::move(downstream));
        });
}

TEMPLATE_TEST_CASE("Histogram in batches, zero bins", "[histogram_in_batches]",
                   saturate_on_overflow, error_on_overflow) {
    auto f = make_histogram_in_batches_fixture<TestType>(0, 0);

    f.feed(other_event{42});
    REQUIRE(f.check(other_event{42}));

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {}, 0, 0}));

    f.feed_end({});
    REQUIRE(f.did_end());
}

TEMPLATE_TEST_CASE("Histogram in batches, no overflow",
                   "[histogram_in_batches]", saturate_on_overflow,
                   error_on_overflow) {
    auto f = make_histogram_in_batches_fixture<TestType>(2, 100);

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1, 0}, 1, 0}));

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {0, 1}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1, 1}, 2, 0}));

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {1, 0}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1, 1}, 2, 0}));

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {1, 1}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {0, 2}, 2, 0}));

    f.feed_end({});
    REQUIRE(f.did_end());
}

TEST_CASE("Histogram in batches, saturate on overflow",
          "[histogram_in_batches]") {
    SECTION("Max per bin = 0") {
        auto f = make_histogram_in_batches_fixture<saturate_on_overflow>(1, 0);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {0}, 1, 1}));

        f.feed_end({});
        REQUIRE(f.did_end());
    }

    SECTION("Max per bin = 1") {
        auto f = make_histogram_in_batches_fixture<saturate_on_overflow>(1, 1);

        f.feed(
            bin_increment_batch_event<unsigned>{42, 43, {0, 0}}); // Overflow
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1}, 2, 1}));

        f.feed_end({});
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Histogram in batches, error on overflow", "[histogram_in_batches") {
    SECTION("Max per bin = 0") {
        auto f = make_histogram_in_batches_fixture<error_on_overflow>(1, 0);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto f = make_histogram_in_batches_fixture<error_on_overflow>(1, 1);

        f.feed(
            bin_increment_batch_event<unsigned>{42, 43, {0, 0}}); // Overflow
        REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
    }
}

template <typename Ovfl>
auto make_accumulate_histograms_fixture(std::size_t n_bins, unsigned bin_max) {
    return make_processor_test_fixture<batch_events,
                                       hist_events>([=](auto &&downstream) {
        return accumulate_histograms<unsigned, unsigned, reset_event, Ovfl>(
            n_bins, bin_max, std::move(downstream));
    });
}

TEMPLATE_TEST_CASE("Accumulate histograms, zero bins",
                   "[accumulate_histograms]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto f = make_accumulate_histograms_fixture<TestType>(0, 0);

    f.feed(other_event{42});
    REQUIRE(f.check(other_event{42}));

    f.feed(reset_event{});
    REQUIRE(f.check(
        accumulated_histogram_event<unsigned>{0, 0, {}, 0, 0, false, false}));

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {}, 0, 0}));

    f.feed(reset_event{});
    REQUIRE(f.check(
        accumulated_histogram_event<unsigned>{42, 43, {}, 0, 0, true, false}));

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {}, 0, 0}));

    f.feed_end({});
    REQUIRE(f.check(
        accumulated_histogram_event<unsigned>{42, 43, {}, 0, 0, true, true}));
    REQUIRE(f.did_end());
}

TEMPLATE_TEST_CASE("Accumulate histograms, no overflow",
                   "[accumulate_histograms]", saturate_on_overflow,
                   reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto f = make_accumulate_histograms_fixture<TestType>(2, 100);

    f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1, 0}, 1, 0}));

    f.feed(bin_increment_batch_event<unsigned>{44, 45, {0, 1}});
    REQUIRE(f.check(histogram_event<unsigned>{42, 45, {2, 1}, 3, 0}));

    f.feed(reset_event{46});
    REQUIRE(f.check(accumulated_histogram_event<unsigned>{
        42, 45, {2, 1}, 3, 0, true, false}));

    f.feed(bin_increment_batch_event<unsigned>{47, 48, {1}});
    REQUIRE(f.check(histogram_event<unsigned>{47, 48, {0, 1}, 1, 0}));

    f.feed_end({});
    REQUIRE(f.check(accumulated_histogram_event<unsigned>{
        47, 48, {0, 1}, 1, 0, true, true}));
    REQUIRE(f.did_end());
}

TEST_CASE("Accumulate histograms, saturate on overflow",
          "[accumulate_histograms]") {
    SECTION("Max per bin = 0") {
        auto f =
            make_accumulate_histograms_fixture<saturate_on_overflow>(1, 0);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {0}, 1, 1}));

        f.feed_end({});
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 43, {0}, 1, 1, true, true}));
        REQUIRE(f.did_end());
    }

    SECTION("Max per bin = 1") {
        auto f =
            make_accumulate_histograms_fixture<saturate_on_overflow>(1, 1);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}});
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1}, 1, 0}));

        f.feed(bin_increment_batch_event<unsigned>{44, 45, {0}}); // Overflow
        REQUIRE(f.check(histogram_event<unsigned>{42, 45, {1}, 2, 1}));

        f.feed(reset_event{46});
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 45, {1}, 2, 1, true, false}));

        f.feed(bin_increment_batch_event<unsigned>{47, 48, {0}});
        REQUIRE(f.check(histogram_event<unsigned>{47, 48, {1}, 1, 0}));

        f.feed_end({});
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            47, 48, {1}, 1, 0, true, true}));
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Accumulate histograms, reset on overflow",
          "[accumulate_histograms]") {
    SECTION("Max per bin = 0") {
        auto f = make_accumulate_histograms_fixture<reset_on_overflow>(1, 0);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto f = make_accumulate_histograms_fixture<reset_on_overflow>(1, 1);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}});
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1}, 1, 0}));

        f.feed(bin_increment_batch_event<unsigned>{44, 45, {0}}); // Overflow
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 43, {1}, 1, 0, true, false}));
        REQUIRE(f.check(histogram_event<unsigned>{44, 45, {1}, 1, 0}));

        SECTION("Normal end") {
            f.feed_end({});
            REQUIRE(f.check(accumulated_histogram_event<unsigned>{
                44, 45, {1}, 1, 0, true, true}));
            REQUIRE(f.did_end());
        }

        SECTION("Error on single-batch overflow") {
            f.feed(bin_increment_batch_event<unsigned>{
                46, 47, {0, 0}}); // Overflow
            // Reset-before-overflow succeeds:
            REQUIRE(f.check(accumulated_histogram_event<unsigned>{
                44, 45, {1}, 1, 0, true, false}));
            // But the batch overflows by itself:
            REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
        }
    }

    SECTION("Roll back batch before resetting") {
        auto f = make_accumulate_histograms_fixture<reset_on_overflow>(2, 1);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {1}});
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {0, 1}, 1, 0}));

        SECTION("Successful reset") {
            f.feed(bin_increment_batch_event<unsigned>{
                44, 45, {0, 1}}); // Overflow
            REQUIRE(f.check(accumulated_histogram_event<unsigned>{
                42, 43, {0, 1}, 1, 0, true, false}));
            REQUIRE(f.check(histogram_event<unsigned>{44, 45, {1, 1}, 2, 0}));

            f.feed_end({});
            REQUIRE(f.check(accumulated_histogram_event<unsigned>{
                44, 45, {1, 1}, 2, 0, true, true}));
            REQUIRE(f.did_end());
        }

        SECTION("Error on single-batch overflow") {
            f.feed(bin_increment_batch_event<unsigned>{
                44, 45, {0, 1, 1}}); // Overflow
            REQUIRE(f.check(accumulated_histogram_event<unsigned>{
                42, 43, {0, 1}, 1, 0, true, false}));
            REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
        }
    }
}

TEST_CASE("Accumulate histograms, stop on overflow",
          "[accumulate_histograms]") {
    SECTION("Max per bin = 0") {
        auto f = make_accumulate_histograms_fixture<stop_on_overflow>(1, 0);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            0, 0, {0}, 0, 0, false, true}));
        REQUIRE(f.did_end());
    }

    SECTION("Max per bin = 1") {
        auto f = make_accumulate_histograms_fixture<stop_on_overflow>(1, 1);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}});
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1}, 1, 0}));

        f.feed(bin_increment_batch_event<unsigned>{44, 45, {0}}); // Overflow
        REQUIRE(f.check(accumulated_histogram_event<unsigned>{
            42, 43, {1}, 1, 0, true, true}));
        REQUIRE(f.did_end());
    }

    SECTION("Roll back batch before stopping") {
        auto f = make_accumulate_histograms_fixture<stop_on_overflow>(2, 1);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {1}});
        REQUIRE(f.check(histogram_event<unsigned>{42, 43, {0, 1}, 1, 0}));

        SECTION("Overflow of accumulated") {
            f.feed(bin_increment_batch_event<unsigned>{
                44, 45, {0, 1}}); // Overflow
            REQUIRE(f.check(accumulated_histogram_event<unsigned>{
                42, 43, {0, 1}, 1, 0, true, true}));
            REQUIRE(f.did_end());
        }

        SECTION("Single-batch overflow") {
            f.feed(bin_increment_batch_event<unsigned>{
                44, 45, {0, 1, 1}}); // Overflow
            REQUIRE(f.check(accumulated_histogram_event<unsigned>{
                42, 43, {0, 1}, 1, 0, true, true}));
            REQUIRE(f.did_end());
        }
    }
}

TEST_CASE("Accumulate histograms, error on overflow",
          "[accumulate_histograms]") {
    SECTION("Max per bin = 0") {
        auto f = make_accumulate_histograms_fixture<error_on_overflow>(1, 0);

        f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
    }

    SECTION("Max per bin = 1") {
        auto f = make_accumulate_histograms_fixture<error_on_overflow>(1, 1);

        SECTION("Overflow of accumulated") {
            f.feed(bin_increment_batch_event<unsigned>{42, 43, {0}});
            REQUIRE(f.check(histogram_event<unsigned>{42, 43, {1}, 1, 0}));

            f.feed(
                bin_increment_batch_event<unsigned>{44, 45, {0}}); // Overflow
            REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
        }

        SECTION("Single-batch overflow") {
            f.feed(bin_increment_batch_event<unsigned>{
                44, 45, {0, 0}}); // Overflow
            REQUIRE_THROWS_AS(f.did_end(), histogram_overflow_error);
        }
    }
}
