/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/histogram_events.hpp"

#include <cstdint>

#include <catch2/catch.hpp>

using namespace flimevt;

TEST_CASE("Journal basic operations", "[bin_increment_batch_journal_event]") {
    bin_increment_batch_journal_event<std::uint16_t> e;
    REQUIRE(e.num_batches() == 0);
    REQUIRE(e.start() == 0);
    REQUIRE(e.stop() == 0);
    REQUIRE(e.begin() == e.end());

    e.append_batch({std::uint16_t(42)});
    REQUIRE(e.num_batches() == 1);

    partial_bin_increment_batch_journal_event<std::uint16_t> e2;
    e.swap(e2);
    REQUIRE(e.num_batches() == 0);
    REQUIRE(e2.num_batches() == 1);
    e2.swap(e);
    REQUIRE(e.num_batches() == 1);
    REQUIRE(e2.num_batches() == 0);

    e.clear();
    REQUIRE(e.num_batches() == 0);

    e.start(100);
    REQUIRE(e.start() == 100);
    e.stop(200);
    REQUIRE(e.stop() == 200);
    e.clear_and_shrink_to_fit();
    REQUIRE(e.start() == 0);
    REQUIRE(e.stop() == 0);
}

TEST_CASE("Journal iterator", "[bin_increment_batch_journal_event]") {
    bin_increment_batch_journal_event<std::uint16_t> e;

    SECTION("Empty") {
        for ([[maybe_unused]] auto [index, begin, end] : e) {
            REQUIRE(false);
        }

        e.append_batch({});
        e.append_batch({});
        REQUIRE(e.num_batches() == 2);
        REQUIRE(e.begin() == e.end());
    }

    SECTION("Start with non-empty batch") {
        e.append_batch({42});
        {
            auto it = e.begin();
            REQUIRE(it != e.end());
            auto [index, begin, end] = *it;
            REQUIRE(index == 0);
            REQUIRE(std::distance(begin, end) == 1);
            REQUIRE(*begin == 42);
        }

        e.append_batch({43, 44});
        {
            auto it = e.begin();
            {
                auto [index, begin, end] = *it;
                REQUIRE(index == 0);
                REQUIRE(std::distance(begin, end) == 1);
                REQUIRE(*begin == 42);
            }
            ++it;
            REQUIRE(it != e.end());
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
        e.append_batch({});
        REQUIRE(e.num_batches() == 1);
        REQUIRE(e.begin() == e.end());
        e.append_batch({42});
        auto it = e.begin();
        REQUIRE(it != e.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 1);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with 2 empty batches") {
        e.append_batch({});
        REQUIRE(e.num_batches() == 1);
        REQUIRE(e.begin() == e.end());
        e.append_batch({});
        REQUIRE(e.num_batches() == 2);
        REQUIRE(e.begin() == e.end());
        e.append_batch({42});
        auto it = e.begin();
        REQUIRE(it != e.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 2);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with 255 empty batches") {
        for (int i = 0; i < 255; ++i)
            e.append_batch({});
        e.append_batch({42});
        REQUIRE(e.num_batches() == 256);
        auto it = e.begin();
        REQUIRE(it != e.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 255);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with 256 empty batches") {
        for (int i = 0; i < 256; ++i)
            e.append_batch({});
        e.append_batch({42});
        REQUIRE(e.num_batches() == 257);
        auto it = e.begin();
        REQUIRE(it != e.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 256);
        REQUIRE(std::distance(begin, end) == 1);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with batch of size 255") {
        std::vector<std::uint16_t> batch(255, 42);
        e.append_batch(batch);
        REQUIRE(e.num_batches() == 1);
        auto it = e.begin();
        REQUIRE(it != e.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 0);
        REQUIRE(std::distance(begin, end) == 255);
        REQUIRE(*begin == 42);
    }

    SECTION("Start with batch of size 256") {
        std::vector<std::uint16_t> batch(256, 42);
        e.append_batch(batch);
        REQUIRE(e.num_batches() == 1);
        auto it = e.begin();
        REQUIRE(it != e.end());
        auto [index, begin, end] = *it;
        REQUIRE(index == 0);
        REQUIRE(std::distance(begin, end) == 256);
        REQUIRE(*begin == 42);
    }

    SECTION("Batch of size 256 following 255 empty batches") {
        std::vector<std::uint16_t> batch(256, 123);
        e.append_batch({42});
        for (int i = 0; i < 255; ++i)
            e.append_batch({});
        e.append_batch(batch);
        REQUIRE(e.num_batches() == 257);
        auto it = e.begin();
        REQUIRE(it != e.end());
        {
            auto [index, begin, end] = *it;
            REQUIRE(index == 0);
            REQUIRE(std::distance(begin, end) == 1);
            REQUIRE(*begin == 42);
        }
        ++it;
        REQUIRE(it != e.end());
        {
            auto [index, begin, end] = *it;
            REQUIRE(index == 256);
            REQUIRE(std::distance(begin, end) == 256);
            REQUIRE(*begin == 123);
        }
    }
}
