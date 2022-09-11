/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/legacy_histogram.hpp"

#include "flimevt/discard.hpp"
#include "flimevt/event_set.hpp"

#include <catch2/catch.hpp>

using namespace flimevt;

static_assert(
    handles_event_set_v<
        histogrammer<unsigned, discard_all<frame_histogram_events<unsigned>>>,
        pixel_photon_events>);

static_assert(handles_event_set_v<
              sequential_histogrammer<
                  unsigned, discard_all<frame_histogram_events<unsigned>>>,
              pixel_photon_events>);

static_assert(
    handles_event_set_v<
        histogram_accumulator<
            unsigned, discard_all<cumulative_histogram_events<unsigned>>>,
        frame_histogram_events<unsigned>>);

TEST_CASE("TimeBins", "[legacy_histogram]") {
    legacy_histogram<std::uint16_t> hist(8, 12, false, 1, 1);
    auto data = hist.get();
    hist.clear();

    hist.increment(0, 0, 0);
    REQUIRE(data[0] == 1);
    hist.increment(15, 0, 0);
    REQUIRE(data[0] == 2);
    hist.increment(16, 0, 0);
    REQUIRE(data[1] == 1);

    hist.increment(4095, 0, 0);
    REQUIRE(data[255] == 1);
    hist.increment(4080, 0, 0);
    REQUIRE(data[255] == 2);
    hist.increment(4079, 0, 0);
    REQUIRE(data[254] == 1);
}

TEST_CASE("ReverseTimeBins", "[legacy_histogram]") {
    legacy_histogram<std::uint16_t> hist(8, 12, true, 1, 1);
    auto data = hist.get();
    hist.clear();

    hist.increment(0, 0, 0);
    REQUIRE(data[255] == 1);
    hist.increment(15, 0, 0);
    REQUIRE(data[255] == 2);
    hist.increment(16, 0, 0);
    REQUIRE(data[254] == 1);

    hist.increment(4095, 0, 0);
    REQUIRE(data[0] == 1);
    hist.increment(4080, 0, 0);
    REQUIRE(data[0] == 2);
    hist.increment(4079, 0, 0);
    REQUIRE(data[1] == 1);
}

TEST_CASE("SingleTimeBin", "[legacy_histogram]") {
    SECTION("Non-reversed") {
        legacy_histogram<std::uint16_t> hist(0, 7, false, 1, 1);
        auto data = hist.get();
        hist.clear();

        hist.increment(0, 0, 0);
        REQUIRE(data[0] == 1);
        hist.increment(127, 0, 0);
        REQUIRE(data[0] == 2);
    }

    SECTION("Reversed") {
        legacy_histogram<std::uint16_t> hist(0, 7, false, 1, 1);
        auto data = hist.get();
        hist.clear();

        hist.increment(0, 0, 0);
        REQUIRE(data[0] == 1);
        hist.increment(127, 0, 0);
        REQUIRE(data[0] == 2);
    }
}
