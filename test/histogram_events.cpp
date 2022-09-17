/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/histogram_events.hpp"

#include <cstdint>

#include <catch2/catch.hpp>

using namespace flimevt;

using u16 = std::uint16_t;

TEST_CASE("Journal basic operations", "[bin_increment_batch_journal]") {
    bin_increment_batch_journal<u16> j;
    REQUIRE(j.num_batches() == 0);
    REQUIRE(j.begin() == j.end());

    j.append_batch({u16(42)});
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
        j.append_batch({42});
        {
            auto it = j.begin();
            REQUIRE(it != j.end());
            auto [index, begin, end] = *it;
            REQUIRE(index == 0);
            REQUIRE(std::distance(begin, end) == 1);
            REQUIRE(*begin == 42);
        }

        j.append_batch({43, 44});
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
        j.append_batch({42});
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
        j.append_batch({42});
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
        j.append_batch({42});
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
        j.append_batch({42});
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
        j.append_batch({42});
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
