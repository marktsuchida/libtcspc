/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/bh_spc.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include <catch2/catch_all.hpp>

using namespace flimevt;

static_assert(sizeof(bh_spc_event) == 4);

namespace {

inline auto bh_spc_event_from_u8(
    std::array<std::uint8_t, sizeof(bh_spc_event)> arr) noexcept
    -> bh_spc_event {
    bh_spc_event ret{};
    std::memcpy(&ret, arr.data(), sizeof(ret));
    return ret;
}

} // namespace

TEST_CASE("adc_value", "[bh_spc_event]") {
    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK(event.get_adc_value() == 0);

    event = bh_spc_event_from_u8({0, 0, 0xff, 0});
    CHECK(event.get_adc_value() == 0xff);

    event = bh_spc_event_from_u8({0, 0, 0xff, 0x0f});
    CHECK(event.get_adc_value() == 4095);

    event = bh_spc_event_from_u8({0, 0, 0, 0x0f});
    CHECK(event.get_adc_value() == 0xf00);

    event = bh_spc_event_from_u8({0xff, 0xff, 0, 0xf0});
    CHECK(event.get_adc_value() == 0);
}

TEST_CASE("routing_signals and marker_bits", "[bh_spc_event]") {
    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK(event.get_routing_signals() == 0);
    CHECK(event.get_marker_bits() == 0);

    event = bh_spc_event_from_u8({0, 0x10, 0, 0});
    CHECK(event.get_routing_signals() == 1);
    CHECK(event.get_marker_bits() == 1);

    event = bh_spc_event_from_u8({0, 0x20, 0, 0});
    CHECK(event.get_routing_signals() == 2);
    CHECK(event.get_marker_bits() == 2);

    event = bh_spc_event_from_u8({0, 0x40, 0, 0});
    CHECK(event.get_routing_signals() == 4);
    CHECK(event.get_marker_bits() == 4);

    event = bh_spc_event_from_u8({0, 0x80, 0, 0});
    CHECK(event.get_routing_signals() == 8);
    CHECK(event.get_marker_bits() == 8);

    event = bh_spc_event_from_u8({0xff, 0x0f, 0xff, 0xff});
    CHECK(event.get_routing_signals() == 0);
    CHECK(event.get_marker_bits() == 0);
}

TEST_CASE("macrotime", "[bh_spc_event]") {
    CHECK(bh_spc_event::macrotime_overflow_period == 4096);

    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK(event.get_macrotime() == 0);

    event = bh_spc_event_from_u8({0xff, 0, 0, 0});
    CHECK(event.get_macrotime() == 0xff);

    event = bh_spc_event_from_u8({0xff, 0x0f, 0, 0});
    CHECK(event.get_macrotime() == 4095);

    event = bh_spc_event_from_u8({0, 0x0f, 0, 0});
    CHECK(event.get_macrotime() == 0xf00);

    event = bh_spc_event_from_u8({0, 0xf0, 0xff, 0xff});
    CHECK(event.get_macrotime() == 0);
}

TEST_CASE("flags", "[bh_spc_event]") {
    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK_FALSE(event.get_invalid_flag());
    CHECK_FALSE(event.get_macrotime_overflow_flag());
    CHECK_FALSE(event.get_gap_flag());
    CHECK_FALSE(event.get_marker_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 7});
    CHECK(event.get_invalid_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 6});
    CHECK(event.get_macrotime_overflow_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 5});
    CHECK(event.get_gap_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 4});
    CHECK(event.get_marker_flag());
}

TEST_CASE("is_multiple_macrotime_overflow", "[bh_spc_event]") {
    bh_spc_event event{};

    // The GAP flag is orthogonal to macrotime overflow. Test all combinations
    // of the other 3 flags. (Although it is expected that INVALID is always
    // set when MARK is set.)
    std::uint8_t const INVALID = 1 << 7;
    std::uint8_t const MTOV = 1 << 6;
    std::uint8_t const MARK = 1 << 4;

    // Valid photon, no overflow
    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK_FALSE(event.is_multiple_macrotime_overflow());

    // Mark, no overflow (not expected)
    event = bh_spc_event_from_u8({0, 0, 0, MARK});
    CHECK_FALSE(event.is_multiple_macrotime_overflow());

    // Valid photon, single overflow
    event = bh_spc_event_from_u8({0, 0, 0, MTOV});
    CHECK_FALSE(event.is_multiple_macrotime_overflow());

    // Marker, single overflow (not expected)
    event = bh_spc_event_from_u8({0, 0, 0, MTOV | MARK});
    CHECK_FALSE(event.is_multiple_macrotime_overflow());

    // Invalid photon, no overflow
    event = bh_spc_event_from_u8({0, 0, 0, INVALID});
    CHECK_FALSE(event.is_multiple_macrotime_overflow());

    // Mark, no overflow
    event = bh_spc_event_from_u8({0, 0, 0, INVALID | MARK});
    CHECK_FALSE(event.is_multiple_macrotime_overflow());

    // Multiple overflow
    event = bh_spc_event_from_u8({0, 0, 0, INVALID | MTOV});
    CHECK(event.is_multiple_macrotime_overflow());

    // Marker, single overflow
    event = bh_spc_event_from_u8({0, 0, 0, INVALID | MTOV | MARK});
    CHECK_FALSE(event.is_multiple_macrotime_overflow());
}

TEST_CASE("multiple_macrotime_overflow_count", "[bh_spc_event]") {
    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK(event.get_multiple_macrotime_overflow_count() == 0);

    event = bh_spc_event_from_u8({1, 0, 0, 0});
    CHECK(event.get_multiple_macrotime_overflow_count() == 1);

    event = bh_spc_event_from_u8({0x80, 0, 0, 0});
    CHECK(event.get_multiple_macrotime_overflow_count() == 128);

    event = bh_spc_event_from_u8({0, 1, 0, 0});
    CHECK(event.get_multiple_macrotime_overflow_count() == 256);

    event = bh_spc_event_from_u8({0, 0x80, 0, 0});
    CHECK(event.get_multiple_macrotime_overflow_count() == 32768);

    event = bh_spc_event_from_u8({0, 0, 1, 0});
    CHECK(event.get_multiple_macrotime_overflow_count() == 65536);

    event = bh_spc_event_from_u8({0, 0, 0x80, 0});
    CHECK(event.get_multiple_macrotime_overflow_count() == 8388608);

    event = bh_spc_event_from_u8({0, 0, 0, 1});
    CHECK(event.get_multiple_macrotime_overflow_count() == 16777216);

    event = bh_spc_event_from_u8({0, 0, 0, 0x08});
    CHECK(event.get_multiple_macrotime_overflow_count() == 134217728);
}
