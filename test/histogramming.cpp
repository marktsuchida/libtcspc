/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/histogramming.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::internal;

using u8 = std::uint8_t;
using u16 = std::uint16_t;

TEST_CASE("Journal basic operations", "[bin_increment_batch_journal]") {
    bin_increment_batch_journal<u16> j;
    REQUIRE(j.num_batches() == 0);
    REQUIRE(j.begin() == j.end());

    j.append_batch(std::array<u16, 1>{42});
    REQUIRE(j.num_batches() == 1);

    bin_increment_batch_journal<u16> j2;
    j.swap(j2);
    REQUIRE(j.num_batches() == 0);
    REQUIRE(j2.num_batches() == 1);
    j2.swap(j);
    REQUIRE(j.num_batches() == 1);
    REQUIRE(j2.num_batches() == 0);

    bin_increment_batch_journal<u16> j3 = j;
    REQUIRE(j3.num_batches() == 1);

    j.clear();
    REQUIRE(j.num_batches() == 0);
    j3.clear_and_shrink_to_fit();
    REQUIRE(j3.num_batches() == 0);
}

TEST_CASE("Journal iterator", "[bin_increment_batch_journal]") {
    bin_increment_batch_journal<u16> j;

    SECTION("Empty") {
        for ([[maybe_unused]] auto [index, begin, end] : j) {
            REQUIRE(false);
        }

        j.append_batch({});
        j.append_batch({});
        REQUIRE(j.num_batches() == 2);
        REQUIRE(j.begin() == j.end());
    }

    SECTION("Start with non-empty batch") {
        j.append_batch(std::array<u16, 1>{42});
        {
            auto it = j.begin();
            REQUIRE(it != j.end());
            auto [index, begin, end] = *it;
            REQUIRE(index == 0);
            REQUIRE(std::distance(begin, end) == 1);
            REQUIRE(*begin == 42);
        }

        j.append_batch(std::array<u16, 2>{43, 44});
        {
            auto it = j.begin();
            {
                auto [index, begin, end] = *it;
                REQUIRE(index == 0);
                REQUIRE(std::distance(begin, end) == 1);
                REQUIRE(*begin == 42);
            }
            ++it;
            REQUIRE(it != j.end());
            {
                auto [index, begin, end] = *it;
                REQUIRE(index == 1);
                REQUIRE(std::distance(begin, end) == 2);
                REQUIRE(*begin == 43);
                ++begin;
                REQUIRE(*begin == 44);
            }
        }
    }

    SECTION("Start with empty batch") {
        j.append_batch({});
        REQUIRE(j.num_batches() == 1);
        REQUIRE(j.begin() == j.end());
        j.append_batch(std::array<u16, 1>{42});
        auto it = j.begin();
        REQUIRE(it != j.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 1);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with 2 empty batches") {
        j.append_batch({});
        REQUIRE(j.num_batches() == 1);
        REQUIRE(j.begin() == j.end());
        j.append_batch({});
        REQUIRE(j.num_batches() == 2);
        REQUIRE(j.begin() == j.end());
        j.append_batch(std::array<u16, 1>{42});
        auto it = j.begin();
        REQUIRE(it != j.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 2);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with 255 empty batches") {
        for (int i = 0; i < 255; ++i)
            j.append_batch({});
        j.append_batch(std::array<u16, 1>{42});
        REQUIRE(j.num_batches() == 256);
        auto it = j.begin();
        REQUIRE(it != j.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 255);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with 256 empty batches") {
        for (int i = 0; i < 256; ++i)
            j.append_batch({});
        j.append_batch(std::array<u16, 1>{42});
        REQUIRE(j.num_batches() == 257);
        auto it = j.begin();
        REQUIRE(it != j.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 256);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with batch of size 255") {
        std::vector<u16> batch(255, 42);
        j.append_batch(batch);
        REQUIRE(j.num_batches() == 1);
        auto it = j.begin();
        REQUIRE(it != j.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 0);
        REQUIRE(std::distance(begin, end) == 255);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with batch of size 256") {
        std::vector<u16> batch(256, 42);
        j.append_batch(batch);
        REQUIRE(j.num_batches() == 1);
        auto it = j.begin();
        REQUIRE(it != j.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 0);
        REQUIRE(std::distance(begin, end) == 256);
        REQUIRE(*begin == 42);
    }

    SECTION("Batch of size 256 following 255 empty batches") {
        std::vector<u16> batch(256, 123);
        j.append_batch(std::array<u16, 1>{42});
        for (int i = 0; i < 255; ++i)
            j.append_batch({});
        j.append_batch(batch);
        REQUIRE(j.num_batches() == 257);
        auto it = j.begin();
        REQUIRE(it != j.end());
        {
            auto [index, begin, end] = *it;
            REQUIRE(index == 0);
            REQUIRE(std::distance(begin, end) == 1);
            REQUIRE(*begin == 42);
        }
        ++it;
        REQUIRE(it != j.end());
        {
            auto [index, begin, end] = *it;
            REQUIRE(index == 256);
            REQUIRE(std::distance(begin, end) == 256);
            REQUIRE(*begin == 123);
        }
    }
}

TEMPLATE_TEST_CASE(
    "single_histogram: construction and destruction do not modify the span",
    "[single_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    auto num_bins = GENERATE(std::size_t{0}, 1, 42, 255);
    auto max_per_bin = GENERATE(u8(0), 1, 255);
    std::vector<u8> data(num_bins, u8(123));
    {
        single_histogram<u8, u8, TestType> const shist(data, max_per_bin);
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 123; }));
    }
    CHECK(
        std::all_of(data.begin(), data.end(), [](u8 e) { return e == 123; }));
}

TEMPLATE_TEST_CASE("single_histogram: clearing zeroes the span",
                   "[single_histogram]", saturate_on_internal_overflow,
                   stop_on_internal_overflow) {
    auto num_bins = GENERATE(std::size_t{0}, 1, 42, 255);
    auto max_per_bin = GENERATE(u8(0), 1, 255);
    std::vector<u8> data(num_bins, u8(123));
    single_histogram<u8, u8, TestType> shist(data, max_per_bin);
    shist.clear();
    CHECK(std::all_of(data.begin(), data.end(), [](u8 e) { return e == 0; }));
}

TEMPLATE_TEST_CASE(
    "single_histogram: non-overflowing increments are correctly applied",
    "[single_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    SECTION("0 bins") {
        single_histogram<u8, u8, TestType> shist({}, 255);
        histogram_stats stats;
        CHECK(shist.apply_increments({}, stats) == 0);
        CHECK(stats.total == 0);
        CHECK(stats.saturated == 0);
    }
    SECTION("1 bin") {
        std::vector<u8> data(1, u8(123));
        single_histogram<u8, u8, TestType> shist(data, 255);
        histogram_stats stats;
        CHECK(shist.apply_increments({}, stats) == 0);
        CHECK(data[0] == 123);
        CHECK(stats.total == 0);
        CHECK(stats.saturated == 0);
        CHECK(shist.apply_increments(std::array<u8, 1>{0}, stats) == 1);
        CHECK(data[0] == 124);
        CHECK(stats.total == 1);
        CHECK(stats.saturated == 0);
    }
    SECTION("Many bins") {
        std::vector<u8> data(256, u8(123));
        single_histogram<u8, u8, TestType> shist(data, 255);
        histogram_stats stats;
        CHECK(shist.apply_increments(std::array<u8, 5>{42, 128, 42, 0, 255},
                                     stats) == 5);
        CHECK(data[0] == 124);
        CHECK(data[42] == 125);
        CHECK(data[128] == 124);
        CHECK(data[255] == 124);
        CHECK(stats.total == 5);
        CHECK(stats.saturated == 0);
    }
}

TEST_CASE("single_histogram: undoing correctly decrements bins and stats",
          "[single_histogram]") {
    SECTION("0 bins") {
        single_histogram<u8, u8, stop_on_internal_overflow> shist({}, 255);
        histogram_stats(stats);
        shist.undo_increments({}, stats);
        CHECK(stats.total == 0);
        CHECK(stats.saturated == 0);
    }
    SECTION("1 bin") {
        std::vector<u8> data(1, u8(123));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(data, 255);
        histogram_stats stats{10, 0};
        shist.undo_increments({}, stats);
        CHECK(data[0] == 123);
        CHECK(stats.total == 10);
        CHECK(stats.saturated == 0);
        shist.undo_increments(std::array<u8, 1>{0}, stats);
        CHECK(data[0] == 122);
        CHECK(stats.total == 9);
        CHECK(stats.saturated == 0);
    }
    SECTION("Many bins") {
        std::vector<u8> data(256, u8(123));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(data, 255);
        histogram_stats stats{10, 0};
        shist.undo_increments(std::array<u8, 5>{42, 128, 42, 0, 255}, stats);
        CHECK(data[0] == 122);
        CHECK(data[42] == 121);
        CHECK(data[128] == 122);
        CHECK(data[255] == 122);
        CHECK(stats.total == 5);
        CHECK(stats.saturated == 0);
    }
}

TEST_CASE("single_histogram: saturate on overflow", "[single_histogram]") {
    histogram_stats stats;
    SECTION("Max per bin of 0") {
        std::vector<u8> data(4, u8(0));
        single_histogram<u8, u8, saturate_on_internal_overflow> shist(data, 0);
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1},
                                     stats) == 7);
        CHECK(stats.total == 7);
        CHECK(stats.saturated == 7);
        CHECK(data == std::vector<u8>{0, 0, 0, 0});
    }
    SECTION("Max per bin in middle of range") {
        std::vector<u8> data(4, u8(123));
        single_histogram<u8, u8, saturate_on_internal_overflow> shist(data,
                                                                      124);
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1},
                                     stats) == 7);
        CHECK(stats.total == 7);
        CHECK(stats.saturated == 3);
        CHECK(data == std::vector<u8>{124, 124, 124, 124});
    }
    SECTION("Max per bin at max representable") {
        std::vector<u8> data(4, u8(254));
        single_histogram<u8, u8, saturate_on_internal_overflow> shist(data,
                                                                      255);
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1},
                                     stats) == 7);
        CHECK(stats.total == 7);
        CHECK(stats.saturated == 3);
        CHECK(data == std::vector<u8>{255, 255, 255, 255});
    }
}

TEST_CASE("single_histogram: stop on overflow", "[single_histogram]") {
    histogram_stats stats;
    SECTION("Max per bin of 0") {
        std::vector<u8> data(4, u8(0));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(data, 0);
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1},
                                     stats) == 0);
        CHECK(stats.total == 0);
        CHECK(stats.saturated == 0);
        CHECK(data == std::vector<u8>{0, 0, 0, 0});
    }
    SECTION("Max per bin in middle of range") {
        std::vector<u8> data(4, u8(123));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(data, 124);
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1},
                                     stats) == 3);
        CHECK(stats.total == 3);
        CHECK(stats.saturated == 0);
        CHECK(data == std::vector<u8>{124, 124, 124, 123});
    }
    SECTION("Max per bin at max representable") {
        std::vector<u8> data(4, u8(254));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(data, 255);
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1},
                                     stats) == 3);
        CHECK(stats.total == 3);
        CHECK(stats.saturated == 0);
        CHECK(data == std::vector<u8>{255, 255, 255, 254});
    }
}

TEMPLATE_TEST_CASE(
    "multi_histogram: construction and destruction do not modify the span",
    "[multi_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    auto num_elements = GENERATE(std::size_t{0}, 1, 42);
    auto num_bins = GENERATE(std::size_t{0}, 1, 42, 255);
    auto max_per_bin = GENERATE(u8(0), 1, 255);
    bool const clear = GENERATE(false, true);
    std::vector<u8> data(num_elements * num_bins, u8(123));
    {
        multi_histogram<u8, u8, TestType> const mhist(
            data, max_per_bin, num_bins, num_elements, clear);
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 123; }));
    }
    CHECK(
        std::all_of(data.begin(), data.end(), [](u8 e) { return e == 123; }));
}

TEMPLATE_TEST_CASE(
    "multi_histogram: zero-element instance behaves as expected",
    "[multi_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    histogram_stats stats; // NOLINT(misc-const-correctness)
    multi_histogram<u8, u8, TestType> mhist({}, 0, 0, 0, true);
    CHECK(not mhist.is_started());
    CHECK(mhist.is_complete());
    CHECK(mhist.is_consistent());
    SECTION("Skip remaining") { mhist.skip_remaining(); }
    SECTION("Roll back") {
        if constexpr (not std::is_same_v<TestType,
                                         saturate_on_internal_overflow>) {
            bin_increment_batch_journal<u8> const journal;
            mhist.roll_back(journal, stats);
        }
    }
    SECTION("Reset") { mhist.reset(true); }
}

TEMPLATE_TEST_CASE(
    "multi_histogram: non-overflowing increments are correctly applied",
    "[multi_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    histogram_stats stats;
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, TestType> mhist(data, 255, 4, 3, false);
    CHECK(mhist.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                      journal));
    CHECK(mhist.apply_increment_batch({}, stats, journal));
    CHECK(mhist.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
    CHECK(mhist.is_complete());
    CHECK(stats.total == 4);
    CHECK(data == std::vector<u8>{124, 124, 123, 124, 123, 123, 123, 123, 123,
                                  124, 123, 123});
}

TEMPLATE_TEST_CASE(
    "multi_histogram: skipping remaining finishes lazy clearing",
    "[multi_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    histogram_stats stats;
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, TestType> mhist(data, 255, 4, 3, true);
    SECTION("Skip all elements") {
        mhist.skip_remaining();
        CHECK(mhist.is_complete());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
        CHECK(stats.total == 0);
    }
    SECTION("Skip some elements") {
        CHECK(mhist.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                          journal));
        mhist.skip_remaining();
        CHECK(mhist.is_complete());
        CHECK(data == std::vector<u8>{1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0});
        CHECK(stats.total == 3);
    }
    SECTION("Skip no elements") {
        CHECK(mhist.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                          journal));
        CHECK(mhist.apply_increment_batch({}, stats, journal));
        CHECK(
            mhist.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
        CHECK(mhist.is_complete());
        mhist.skip_remaining();
        CHECK(mhist.is_complete());
        CHECK(data == std::vector<u8>{1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0});
        CHECK(stats.total == 4);
    }
}

TEST_CASE("multi_histogram: rolling back works and applies clearing",
          "[multi_histogram]") {
    histogram_stats stats;
    bin_increment_batch_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, stop_on_internal_overflow> mhist(data, 255, 4, 3,
                                                             true);
    CHECK(not mhist.is_consistent());
    SECTION("Roll back no elements") {
        mhist.roll_back(journal, stats);
        CHECK(not mhist.is_started());
        CHECK(mhist.is_consistent());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
        CHECK(stats.total == 0);
    }
    SECTION("Roll back some elements") {
        CHECK(mhist.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                          journal));
        CHECK(mhist.is_started());
        mhist.roll_back(journal, stats);
        CHECK(not mhist.is_started());
        CHECK(mhist.is_consistent());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
        CHECK(stats.total == 0);
    }
    SECTION("Roll back all elements") {
        CHECK(mhist.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                          journal));
        CHECK(mhist.apply_increment_batch({}, stats, journal));
        CHECK(
            mhist.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
        CHECK(mhist.is_complete());
        mhist.roll_back(journal, stats);
        CHECK(mhist.is_consistent());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
        CHECK(stats.total == 0);
    }
}

TEST_CASE("multi_histogram: replay reproduces journaled data",
          "[multi_histogram]") {
    histogram_stats stats;
    bin_increment_batch_journal<u8> journal;
    std::vector<u8> data(12, u8(0));
    multi_histogram<u8, u8, stop_on_internal_overflow> mhist(data, 255, 4, 3,
                                                             true);
    CHECK(mhist.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                      journal));
    CHECK(mhist.apply_increment_batch(std::array<u8, 1>{2}, stats, journal));
    auto data_copy = data;
    mhist.reset(true);
    histogram_stats stats2;
    mhist.replay(journal, stats2);
    CHECK(data == data_copy);
    CHECK(stats2 == stats);
}

TEST_CASE("multi_histogram: saturate on overflow", "[multi_histogram]") {
    histogram_stats stats;
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, saturate_on_internal_overflow> mhist(data, 124, 4,
                                                                 3, false);
    CHECK(mhist.apply_increment_batch(std::array<u8, 4>{1, 0, 1, 3}, stats,
                                      journal));
    CHECK(mhist.apply_increment_batch({}, stats, journal));
    CHECK(mhist.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
    CHECK(data == std::vector<u8>{124, 124, 123, 124, 123, 123, 123, 123, 123,
                                  124, 123, 123});
    CHECK(stats.total == 5);
    CHECK(stats.saturated == 1);
}

TEST_CASE("multi_histogram: stop on overflow", "[multi_histogram]") {
    histogram_stats stats;
    bin_increment_batch_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, stop_on_internal_overflow> mhist(data, 1, 4, 3,
                                                             true);
    CHECK(
        mhist.apply_increment_batch(std::array<u8, 2>{2, 1}, stats, journal));
    CHECK(not mhist.apply_increment_batch(std::array<u8, 4>{1, 0, 1, 3}, stats,
                                          journal));
    CHECK(mhist.is_complete());
    CHECK(data == std::vector<u8>{0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(stats.total == 2);
    CHECK(stats.saturated == 0);
    mhist.roll_back(journal, stats);
    CHECK(stats.total == 0);
    CHECK(std::all_of(data.begin(), data.end(), [](u8 e) { return e == 0; }));
}

TEMPLATE_TEST_CASE(
    "multi_histogram_accumulation: construction and destruction do not modify the span",
    "[multi_histogram_accumulation]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    auto num_elements = GENERATE(std::size_t{0}, 1, 42);
    auto num_bins = GENERATE(std::size_t{0}, 1, 42, 255);
    auto max_per_bin = GENERATE(u8(0), 1, 255);
    bool const clear_first = GENERATE(false, true);
    std::vector<u8> data(num_elements * num_bins, u8(123));
    {
        multi_histogram_accumulation<u8, u8, TestType> const mhista(
            data, max_per_bin, num_bins, num_elements, clear_first);
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 123; }));
    }
    CHECK(
        std::all_of(data.begin(), data.end(), [](u8 e) { return e == 123; }));
}

TEMPLATE_TEST_CASE(
    "multi_histogram_accumulation: zero-element instance behaves as expected",
    "[multi_histogram_accumulation]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    histogram_stats stats; // NOLINT(misc-const-correctness)
    multi_histogram_accumulation<u8, u8, TestType> mhista({}, 0, 0, 0, true);
    CHECK(mhista.is_cycle_complete());
    CHECK(mhista.is_consistent());
    bin_increment_batch_journal<u8> journal;
    SECTION("New cycle") {
        mhista.new_cycle(journal);
        CHECK(mhista.is_cycle_complete());
    }
    SECTION("Skip remainder") { mhista.skip_remainder_of_current_cycle(); }
    SECTION("Roll back") {
        if constexpr (not std::is_same_v<TestType,
                                         saturate_on_internal_overflow>)
            mhista.roll_back_current_cycle(journal, stats);
    }
    SECTION("Reset") { mhista.reset(true); }
}

TEMPLATE_TEST_CASE(
    "multi_histogram_accumulation: non-overflowing increments are correctly applied",
    "[multi_histogram_accumulation]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    histogram_stats stats;
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, TestType> mhista(data, 255, 4, 3,
                                                          false);
    CHECK(mhista.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                       journal));
    CHECK(mhista.apply_increment_batch({}, stats, journal));
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
    CHECK(mhista.is_cycle_complete());
    mhista.new_cycle(journal);
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{2}, stats, journal));
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{3}, stats, journal));
    CHECK(stats.total == 7);
    CHECK(data == std::vector<u8>{124, 124, 124, 124, 123, 124, 123, 123, 123,
                                  124, 123, 124});
}

TEMPLATE_TEST_CASE(
    "multi_histogram_accumulation: skipping preserves previous cycle",
    "[multi_histogram_accumulation]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    histogram_stats stats;
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, TestType> mhista(data, 255, 4, 3,
                                                          true);
    CHECK(mhista.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                       journal));
    CHECK(mhista.apply_increment_batch({}, stats, journal));
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
    mhista.new_cycle(journal);
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{2}, stats, journal));
    mhista.skip_remainder_of_current_cycle();
    CHECK(mhista.is_cycle_complete());
    CHECK(stats.total == 5);
    CHECK(data == std::vector<u8>{1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0});
}

TEST_CASE("multi_histogram_accumulation: rolling back restores previous cycle",
          "[multi_histogram_accumulation]") {
    histogram_stats stats;
    bin_increment_batch_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, stop_on_internal_overflow> mhista(
        data, 255, 4, 3, true);
    CHECK(mhista.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                       journal));
    CHECK(mhista.apply_increment_batch({}, stats, journal));
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
    mhista.new_cycle(journal);
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{2}, stats, journal));
    mhista.roll_back_current_cycle(journal, stats);
    CHECK(mhista.is_consistent());
    CHECK(stats.total == 4);
    CHECK(data == std::vector<u8>{1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0});
}

TEST_CASE("multi_histogram_accumulation: replay reproduces rolled-back delta",
          "[multi_histogram_accumulation]") {
    histogram_stats stats;
    bin_increment_batch_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, stop_on_internal_overflow> mhista(
        data, 255, 4, 3, true);
    CHECK(mhista.apply_increment_batch(std::array<u8, 3>{0, 1, 3}, stats,
                                       journal));
    CHECK(mhista.apply_increment_batch({}, stats, journal));
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{1}, stats, journal));
    mhista.new_cycle(journal);
    CHECK(mhista.apply_increment_batch(std::array<u8, 1>{2}, stats, journal));
    histogram_stats stats2;
    mhista.reset_and_replay(journal, stats2);
    CHECK(stats2.total == 1);
    mhista.skip_remainder_of_current_cycle();
    CHECK(data == std::vector<u8>{0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}
