/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogramming.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/int_types.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace tcspc::internal {

TEST_CASE("Journal basic operations") {
    bin_increment_cluster_journal<u16> j;
    REQUIRE(j.empty());
    REQUIRE(j.begin() == j.end());

    j.append_cluster(std::array<u16, 1>{42});
    REQUIRE(j.size() == 1);

    using std::swap;
    bin_increment_cluster_journal<u16> j2;
    swap(j, j2);
    REQUIRE(j.empty());
    REQUIRE(j2.size() == 1);
    swap(j2, j);
    REQUIRE(j.size() == 1);
    REQUIRE(j2.empty());

    bin_increment_cluster_journal<u16> j3 = j;
    REQUIRE(j3.size() == 1);

    j.clear();
    REQUIRE(j.empty());
    j3.clear();
    REQUIRE(j3.empty());
}

TEST_CASE("Journal iterator") {
    bin_increment_cluster_journal<u8> j;

    SECTION("Empty") {
        for ([[maybe_unused]] auto cluster : j) {
            REQUIRE(false);
        }

        j.append_cluster({});
        j.append_cluster({});
        REQUIRE(j.size() == 2);
    }

    SECTION("Start with non-empty cluster") {
        j.append_cluster(std::array<u8, 1>{42});
        {
            auto it = j.begin();
            REQUIRE(it != j.end());
            auto cluster = *it;
            REQUIRE(cluster.size() == 1);
            REQUIRE(cluster[0] == 42);
        }

        j.append_cluster(std::array<u8, 2>{43, 44});
        {
            auto it = j.begin();
            {
                auto cluster = *it;
                REQUIRE(cluster.size() == 1);
                REQUIRE(cluster[0] == 42);
            }
            ++it;
            REQUIRE(it != j.end());
            {
                auto cluster = *it;
                REQUIRE(cluster.size() == 2);
                REQUIRE(cluster[0] == 43);
                REQUIRE(cluster[1] == 44);
            }
        }
    }

    SECTION("Start with empty cluster") {
        j.append_cluster({});
        REQUIRE(j.size() == 1);
        j.append_cluster(std::array<u8, 1>{42});
        auto it = j.begin();
        REQUIRE(it != j.end());
        auto cluster = *it++;
        REQUIRE(cluster.empty());
        cluster = *it++;
        REQUIRE(cluster.size() == 1);
        REQUIRE(cluster[0] == 42);
        REQUIRE(it == j.end());
    }

    SECTION("Start with cluster of size 254") {
        std::vector<u8> cluster(254, 42);
        j.append_cluster(cluster);
        REQUIRE(j.size() == 1);
        auto b = *(j.begin());
        REQUIRE(b.size() == 254);
        REQUIRE(b[0] == 42);
        REQUIRE(b[253] == 42);
    }

    SECTION("Start with cluster of size 255") {
        std::vector<u8> cluster(255, 42);
        j.append_cluster(cluster);
        REQUIRE(j.size() == 1);
        auto b = *(j.begin());
        REQUIRE(b.size() == 255);
        REQUIRE(b[0] == 42);
        REQUIRE(b[254] == 42);
    }

    SECTION("Start with cluster of size 256") {
        std::vector<u8> cluster(256, 42);
        j.append_cluster(cluster);
        REQUIRE(j.size() == 1);
        auto b = *(j.begin());
        REQUIRE(b.size() == 256);
        REQUIRE(b[0] == 42);
        REQUIRE(b[255] == 42);
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
        single_histogram<u8, u8, TestType> const shist(
            data, arg::max_per_bin{max_per_bin}, arg::num_bins{num_bins});
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
    single_histogram<u8, u8, TestType> shist(
        data, arg::max_per_bin{max_per_bin}, arg::num_bins{num_bins});
    shist.clear();
    CHECK(std::all_of(data.begin(), data.end(), [](u8 e) { return e == 0; }));
}

TEMPLATE_TEST_CASE(
    "single_histogram: non-overflowing increments are correctly applied",
    "[single_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    SECTION("1 bin") {
        std::vector<u8> data(1, u8(123));
        single_histogram<u8, u8, TestType> shist(
            data, arg::max_per_bin<u8>{255}, arg::num_bins<>{1});
        CHECK(shist.apply_increments({}) == 0);
        CHECK(data[0] == 123);
        CHECK(shist.apply_increments(std::array<u8, 1>{0}) == 1);
        CHECK(data[0] == 124);
    }
    SECTION("Many bins") {
        std::vector<u8> data(256, u8(123));
        single_histogram<u8, u8, TestType> shist(
            data, arg::max_per_bin<u8>{255}, arg::num_bins{data.size()});
        CHECK(shist.apply_increments(std::array<u8, 5>{42, 128, 42, 0, 255}) ==
              5);
        CHECK(data[0] == 124);
        CHECK(data[42] == 125);
        CHECK(data[128] == 124);
        CHECK(data[255] == 124);
    }
}

TEST_CASE("single_histogram: undoing correctly decrements bins",
          "[single_histogram]") {
    SECTION("1 bin") {
        std::vector<u8> data(1, u8(123));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{255}, arg::num_bins{data.size()});
        shist.undo_increments({});
        CHECK(data[0] == 123);
        shist.undo_increments(std::array<u8, 1>{0});
        CHECK(data[0] == 122);
    }
    SECTION("Many bins") {
        std::vector<u8> data(256, u8(123));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{255}, arg::num_bins{data.size()});
        shist.undo_increments(std::array<u8, 5>{42, 128, 42, 0, 255});
        CHECK(data[0] == 122);
        CHECK(data[42] == 121);
        CHECK(data[128] == 122);
        CHECK(data[255] == 122);
    }
}

TEST_CASE("single_histogram: saturate on overflow") {
    SECTION("Max per bin of 0") {
        std::vector<u8> data(4, u8(0));
        single_histogram<u8, u8, saturate_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{0}, arg::num_bins{data.size()});
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1}) ==
              0);
        CHECK(data == std::vector<u8>{0, 0, 0, 0});
    }
    SECTION("Max per bin in middle of range") {
        std::vector<u8> data(4, u8(123));
        single_histogram<u8, u8, saturate_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{124}, arg::num_bins{data.size()});
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1}) ==
              4);
        CHECK(data == std::vector<u8>{124, 124, 124, 124});
    }
    SECTION("Max per bin at max representable") {
        std::vector<u8> data(4, u8(254));
        single_histogram<u8, u8, saturate_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{255}, arg::num_bins{data.size()});
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1}) ==
              4);
        CHECK(data == std::vector<u8>{255, 255, 255, 255});
    }
}

TEST_CASE("single_histogram: stop on overflow") {
    SECTION("Max per bin of 0") {
        std::vector<u8> data(4, u8(0));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{0}, arg::num_bins{data.size()});
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1}) ==
              0);
        CHECK(data == std::vector<u8>{0, 0, 0, 0});
    }
    SECTION("Max per bin in middle of range") {
        std::vector<u8> data(4, u8(123));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{124}, arg::num_bins{data.size()});
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1}) ==
              3);
        CHECK(data == std::vector<u8>{124, 124, 124, 123});
    }
    SECTION("Max per bin at max representable") {
        std::vector<u8> data(4, u8(254));
        single_histogram<u8, u8, stop_on_internal_overflow> shist(
            data, arg::max_per_bin<u8>{255}, arg::num_bins{data.size()});
        CHECK(shist.apply_increments(std::array<u8, 7>{0, 1, 2, 1, 3, 3, 1}) ==
              3);
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
            data, arg::max_per_bin{max_per_bin}, arg::num_bins{num_bins},
            arg::num_elements{num_elements}, clear);
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 123; }));
    }
    CHECK(
        std::all_of(data.begin(), data.end(), [](u8 e) { return e == 123; }));
}

TEMPLATE_TEST_CASE(
    "multi_histogram: non-overflowing increments are correctly applied",
    "[multi_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, TestType> mhist(data, arg::max_per_bin<u8>{255},
                                            arg::num_bins<>{4},
                                            arg::num_elements<>{3}, false);
    CHECK(mhist.apply_increment_cluster(std::array<u8, 3>{0, 1, 3}, journal));
    CHECK(mhist.apply_increment_cluster({}, journal));
    CHECK(mhist.apply_increment_cluster(std::array<u8, 1>{1}, journal));
    CHECK(mhist.is_complete());
    CHECK(data == std::vector<u8>{124, 124, 123, 124, 123, 123, 123, 123, 123,
                                  124, 123, 123});
}

TEMPLATE_TEST_CASE(
    "multi_histogram: skipping remaining finishes lazy clearing",
    "[multi_histogram]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, TestType> mhist(data, arg::max_per_bin<u8>{255},
                                            arg::num_bins<>{4},
                                            arg::num_elements<>{3}, true);
    SECTION("Skip all elements") {
        mhist.skip_remaining();
        CHECK(mhist.is_complete());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
    }
    SECTION("Skip some elements") {
        CHECK(mhist.apply_increment_cluster(std::array<u8, 3>{0, 1, 3},
                                            journal));
        mhist.skip_remaining();
        CHECK(mhist.is_complete());
        CHECK(data == std::vector<u8>{1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0});
    }
    SECTION("Skip no elements") {
        CHECK(mhist.apply_increment_cluster(std::array<u8, 3>{0, 1, 3},
                                            journal));
        CHECK(mhist.apply_increment_cluster({}, journal));
        CHECK(mhist.apply_increment_cluster(std::array<u8, 1>{1}, journal));
        CHECK(mhist.is_complete());
        mhist.skip_remaining();
        CHECK(mhist.is_complete());
        CHECK(data == std::vector<u8>{1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0});
    }
}

TEST_CASE("multi_histogram: rolling back works and applies clearing",
          "[multi_histogram]") {
    bin_increment_cluster_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, stop_on_internal_overflow> mhist(
        data, arg::max_per_bin<u8>{255}, arg::num_bins<>{4},
        arg::num_elements<>{3}, true);
    CHECK_FALSE(mhist.is_consistent());
    SECTION("Roll back no elements") {
        mhist.roll_back(journal);
        CHECK_FALSE(mhist.is_started());
        CHECK(mhist.is_consistent());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
    }
    SECTION("Roll back some elements") {
        CHECK(mhist.apply_increment_cluster(std::array<u8, 3>{0, 1, 3},
                                            journal));
        CHECK(mhist.is_started());
        mhist.roll_back(journal);
        CHECK_FALSE(mhist.is_started());
        CHECK(mhist.is_consistent());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
    }
    SECTION("Roll back all elements") {
        CHECK(mhist.apply_increment_cluster(std::array<u8, 3>{0, 1, 3},
                                            journal));
        CHECK(mhist.apply_increment_cluster({}, journal));
        CHECK(mhist.apply_increment_cluster(std::array<u8, 1>{1}, journal));
        CHECK(mhist.is_complete());
        mhist.roll_back(journal);
        CHECK(mhist.is_consistent());
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 0; }));
    }
}

TEST_CASE("multi_histogram: replay reproduces journaled data",
          "[multi_histogram]") {
    bin_increment_cluster_journal<u8> journal;
    std::vector<u8> data(12, u8(0));
    multi_histogram<u8, u8, stop_on_internal_overflow> mhist(
        data, arg::max_per_bin<u8>{255}, arg::num_bins<>{4},
        arg::num_elements<>{3}, true);
    CHECK(mhist.apply_increment_cluster(std::array<u8, 3>{0, 1, 3}, journal));
    CHECK(mhist.apply_increment_cluster(std::array<u8, 1>{2}, journal));
    auto data_copy = data;
    mhist.reset(true);
    mhist.replay(journal);
    CHECK(data == data_copy);
}

TEST_CASE("multi_histogram: saturate on overflow") {
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, saturate_on_internal_overflow> mhist(
        data, arg::max_per_bin<u8>{124}, arg::num_bins<>{4},
        arg::num_elements<>{3}, false);
    CHECK_FALSE(
        mhist.apply_increment_cluster(std::array<u8, 4>{1, 0, 1, 3}, journal));
    CHECK(mhist.apply_increment_cluster({}, journal));
    CHECK(mhist.apply_increment_cluster(std::array<u8, 1>{1}, journal));
    CHECK(data == std::vector<u8>{124, 124, 123, 124, 123, 123, 123, 123, 123,
                                  124, 123, 123});
}

TEST_CASE("multi_histogram: stop on overflow") {
    bin_increment_cluster_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram<u8, u8, stop_on_internal_overflow> mhist(
        data, arg::max_per_bin<u8>{1}, arg::num_bins<>{4},
        arg::num_elements<>{3}, true);
    CHECK(mhist.apply_increment_cluster(std::array<u8, 2>{2, 1}, journal));
    CHECK_FALSE(
        mhist.apply_increment_cluster(std::array<u8, 4>{1, 0, 1, 3}, journal));
    CHECK(mhist.is_complete());
    CHECK(data == std::vector<u8>{0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    mhist.roll_back(journal);
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
            data, arg::max_per_bin{max_per_bin}, arg::num_bins{num_bins},
            arg::num_elements{num_elements}, clear_first);
        CHECK(std::all_of(data.begin(), data.end(),
                          [](u8 e) { return e == 123; }));
    }
    CHECK(
        std::all_of(data.begin(), data.end(), [](u8 e) { return e == 123; }));
}

TEMPLATE_TEST_CASE(
    "multi_histogram_accumulation: non-overflowing increments are correctly applied",
    "[multi_histogram_accumulation]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, TestType> mhista(
        data, arg::max_per_bin<u8>{255}, arg::num_bins<>{4},
        arg::num_elements<>{3}, false);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 3>{0, 1, 3}, journal));
    CHECK(mhista.apply_increment_cluster({}, journal));
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{1}, journal));
    CHECK(mhista.is_scan_complete());
    mhista.new_scan(journal);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{2}, journal));
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{1}, journal));
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{3}, journal));
    CHECK(data == std::vector<u8>{124, 124, 124, 124, 123, 124, 123, 123, 123,
                                  124, 123, 124});
}

TEMPLATE_TEST_CASE(
    "multi_histogram_accumulation: skipping preserves previous scan",
    "[multi_histogram_accumulation]", saturate_on_internal_overflow,
    stop_on_internal_overflow) {
    null_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, TestType> mhista(
        data, arg::max_per_bin<u8>{255}, arg::num_bins<>{4},
        arg::num_elements<>{3}, true);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 3>{0, 1, 3}, journal));
    CHECK(mhista.apply_increment_cluster({}, journal));
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{1}, journal));
    mhista.new_scan(journal);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{2}, journal));
    mhista.skip_remainder_of_current_scan();
    CHECK(mhista.is_scan_complete());
    CHECK(data == std::vector<u8>{1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0});
}

TEST_CASE("multi_histogram_accumulation: rolling back restores previous scan",
          "[multi_histogram_accumulation]") {
    bin_increment_cluster_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, stop_on_internal_overflow> mhista(
        data, arg::max_per_bin<u8>{255}, arg::num_bins<>{4},
        arg::num_elements<>{3}, true);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 3>{0, 1, 3}, journal));
    CHECK(mhista.apply_increment_cluster({}, journal));
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{1}, journal));
    mhista.new_scan(journal);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{2}, journal));
    mhista.roll_back_current_scan(journal);
    CHECK(mhista.is_consistent());
    CHECK(data == std::vector<u8>{1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0});
}

TEST_CASE("multi_histogram_accumulation: replay reproduces rolled-back delta",
          "[multi_histogram_accumulation]") {
    bin_increment_cluster_journal<u8> journal;
    std::vector<u8> data(12, u8(123));
    multi_histogram_accumulation<u8, u8, stop_on_internal_overflow> mhista(
        data, arg::max_per_bin<u8>{255}, arg::num_bins<>{4},
        arg::num_elements<>{3}, true);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 3>{0, 1, 3}, journal));
    CHECK(mhista.apply_increment_cluster({}, journal));
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{1}, journal));
    mhista.new_scan(journal);
    CHECK(mhista.apply_increment_cluster(std::array<u8, 1>{2}, journal));
    mhista.reset(true);
    mhista.replay(journal);
    mhista.skip_remainder_of_current_scan();
    CHECK(data == std::vector<u8>{0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0});
}

} // namespace tcspc::internal
