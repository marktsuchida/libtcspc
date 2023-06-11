/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/bh_spc.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch.hpp>

using namespace flimevt;

static_assert(sizeof(bh_spc_event) == 4);

TEST_CASE("ADCValue", "[bh_spc_event]") {
    bh_spc_event event{};

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0}.data(), 4);
    REQUIRE(event.get_adc_value() == 0);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0xff, 0}.data(), 4);
    REQUIRE(event.get_adc_value() == 0xff);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0xff, 0x0f}.data(),
                4);
    REQUIRE(event.get_adc_value() == 4095);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0x0f}.data(), 4);
    REQUIRE(event.get_adc_value() == 0xf00);

    std::memcpy(&event,
                std::array<std::uint8_t, 4>{0xff, 0xff, 0, 0xf0}.data(), 4);
    REQUIRE(event.get_adc_value() == 0);
}

TEST_CASE("RoutingSignals", "[bh_spc_event]") {
    bh_spc_event event{};

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0}.data(), 4);
    REQUIRE(event.get_routing_signals() == 0);
    REQUIRE(event.get_marker_bits() == 0);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0x10, 0, 0}.data(), 4);
    REQUIRE(event.get_routing_signals() == 1);
    REQUIRE(event.get_marker_bits() == 1);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0x20, 0, 0}.data(), 4);
    REQUIRE(event.get_routing_signals() == 2);
    REQUIRE(event.get_marker_bits() == 2);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0x40, 0, 0}.data(), 4);
    REQUIRE(event.get_routing_signals() == 4);
    REQUIRE(event.get_marker_bits() == 4);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0x80, 0, 0}.data(), 4);
    REQUIRE(event.get_routing_signals() == 8);
    REQUIRE(event.get_marker_bits() == 8);

    std::memcpy(&event,
                std::array<std::uint8_t, 4>{0xff, 0x0f, 0xff, 0xff}.data(), 4);
    REQUIRE(event.get_routing_signals() == 0);
    REQUIRE(event.get_marker_bits() == 0);
}

TEST_CASE("Macrotime", "[bh_spc_event]") {
    REQUIRE(bh_spc_event::macrotime_overflow_period == 4096);

    bh_spc_event event{};

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0}.data(), 4);
    REQUIRE(event.get_macrotime() == 0);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0xff, 0, 0, 0}.data(), 4);
    REQUIRE(event.get_macrotime() == 0xff);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0xff, 0x0f, 0, 0}.data(),
                4);
    REQUIRE(event.get_macrotime() == 4095);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0x0f, 0, 0}.data(), 4);
    REQUIRE(event.get_macrotime() == 0xf00);

    std::memcpy(&event,
                std::array<std::uint8_t, 4>{0, 0xf0, 0xff, 0xff}.data(), 4);
    REQUIRE(event.get_macrotime() == 0);
}

TEST_CASE("Flags", "[bh_spc_event]") {
    bh_spc_event event{};

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0}.data(), 4);
    REQUIRE_FALSE(event.get_invalid_flag());
    REQUIRE_FALSE(event.get_macrotime_overflow_flag());
    REQUIRE_FALSE(event.get_gap_flag());
    REQUIRE_FALSE(event.get_marker_flag());

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 1 << 7}.data(),
                4);
    REQUIRE(event.get_invalid_flag());
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 1 << 6}.data(),
                4);
    REQUIRE(event.get_macrotime_overflow_flag());
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 1 << 5}.data(),
                4);
    REQUIRE(event.get_gap_flag());
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 1 << 4}.data(),
                4);
    REQUIRE(event.get_marker_flag());
}

TEST_CASE("MacrotimeOverflow", "[bh_spc_event]") {
    bh_spc_event event{};

    // The GAP flag is orthogonal to macrotime overflow. Test all combinations
    // of the other 3 flags. (Although it is expected that INVALID is always
    // set when MARK is set.)
    std::uint8_t const INVALID = 1 << 7;
    std::uint8_t const MTOV = 1 << 6;
    std::uint8_t const MARK = 1 << 4;

    // Valid photon, no overflow
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0}.data(), 4);
    REQUIRE_FALSE(event.is_multiple_macrotime_overflow());

    // Mark, no overflow (not expected)
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, MARK}.data(), 4);
    REQUIRE_FALSE(event.is_multiple_macrotime_overflow());

    // Valid photon, single overflow
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, MTOV}.data(), 4);
    REQUIRE_FALSE(event.is_multiple_macrotime_overflow());

    // Marker, single overflow (not expected)
    std::memcpy(&event,
                std::array<std::uint8_t, 4>{0, 0, 0, MTOV | MARK}.data(), 4);
    REQUIRE_FALSE(event.is_multiple_macrotime_overflow());

    // Invalid photon, no overflow
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, INVALID}.data(),
                4);
    REQUIRE_FALSE(event.is_multiple_macrotime_overflow());

    // Mark, no overflow
    std::memcpy(&event,
                std::array<std::uint8_t, 4>{0, 0, 0, INVALID | MARK}.data(),
                4);
    REQUIRE_FALSE(event.is_multiple_macrotime_overflow());

    // Multiple overflow
    std::memcpy(&event,
                std::array<std::uint8_t, 4>{0, 0, 0, INVALID | MTOV}.data(),
                4);
    REQUIRE(event.is_multiple_macrotime_overflow());

    // Marker, single overflow
    std::memcpy(
        &event,
        std::array<std::uint8_t, 4>{0, 0, 0, INVALID | MTOV | MARK}.data(), 4);
    REQUIRE_FALSE(event.is_multiple_macrotime_overflow());
}

TEST_CASE("MacrotimeOverflowCount", "[bh_spc_event]") {
    bh_spc_event event{};

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 0);

    std::memcpy(&event, std::array<std::uint8_t, 4>{1, 0, 0, 0}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 1);
    std::memcpy(&event, std::array<std::uint8_t, 4>{0x80, 0, 0, 0}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 128);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 1, 0, 0}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 256);
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0x80, 0, 0}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 32768);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 1, 0}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 65536);
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0x80, 0}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 8388608);
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0}.data(), 4);

    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 1}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 16777216);
    std::memcpy(&event, std::array<std::uint8_t, 4>{0, 0, 0, 0x08}.data(), 4);
    REQUIRE(event.get_multiple_macrotime_overflow_count() == 134217728);
}
