/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/bh_spc.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <catch2/catch_all.hpp>

namespace tcspc {

static_assert(std::is_trivial_v<bh_spc_event>);
static_assert(std::is_trivial_v<bh_spc_600_event_32>);
static_assert(std::is_trivial_v<bh_spc_600_event_48>);

static_assert(sizeof(bh_spc_event) == 4);
static_assert(sizeof(bh_spc_600_event_32) == 4);
static_assert(sizeof(bh_spc_600_event_48) == 6);

namespace {

inline auto bh_spc_event_from_u8(
    std::array<std::uint8_t, sizeof(bh_spc_event)> arr) noexcept
    -> bh_spc_event {
    bh_spc_event ret{};
    std::memcpy(&ret, arr.data(), sizeof(ret));
    return ret;
}

} // namespace

TEST_CASE("bh_spc_event equality", "[bh_spc_event]") {
    bh_spc_event a{};
    bh_spc_event b{};
    CHECK(a == b);
    CHECK_FALSE(a != b);
}

TEST_CASE("adc_value", "[bh_spc_event]") {
    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK(event.adc_value() == 0_u16np);

    event = bh_spc_event_from_u8({0, 0, 0xff, 0});
    CHECK(event.adc_value() == 0xff_u16np);

    event = bh_spc_event_from_u8({0, 0, 0xff, 0x0f});
    CHECK(event.adc_value() == 4095_u16np);

    event = bh_spc_event_from_u8({0, 0, 0, 0x0f});
    CHECK(event.adc_value() == 0xf00_u16np);

    event = bh_spc_event_from_u8({0xff, 0xff, 0, 0xf0});
    CHECK(event.adc_value() == 0_u16np);
}

TEST_CASE("routing_signals and marker_bits", "[bh_spc_event]") {
    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK(event.routing_signals() == 0_u8np);
    CHECK(event.marker_bits() == 0_u8np);

    event = bh_spc_event_from_u8({0, 0x10, 0, 0});
    CHECK(event.routing_signals() == 1_u8np);
    CHECK(event.marker_bits() == 1_u8np);

    event = bh_spc_event_from_u8({0, 0x20, 0, 0});
    CHECK(event.routing_signals() == 2_u8np);
    CHECK(event.marker_bits() == 2_u8np);

    event = bh_spc_event_from_u8({0, 0x40, 0, 0});
    CHECK(event.routing_signals() == 4_u8np);
    CHECK(event.marker_bits() == 4_u8np);

    event = bh_spc_event_from_u8({0, 0x80, 0, 0});
    CHECK(event.routing_signals() == 8_u8np);
    CHECK(event.marker_bits() == 8_u8np);

    event = bh_spc_event_from_u8({0xff, 0x0f, 0xff, 0xff});
    CHECK(event.routing_signals() == 0_u8np);
    CHECK(event.marker_bits() == 0_u8np);
}

TEST_CASE("macrotime", "[bh_spc_event]") {
    CHECK(bh_spc_event::macrotime_overflow_period == 4096);

    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK(event.macrotime() == 0_u16np);

    event = bh_spc_event_from_u8({0xff, 0, 0, 0});
    CHECK(event.macrotime() == 0xff_u16np);

    event = bh_spc_event_from_u8({0xff, 0x0f, 0, 0});
    CHECK(event.macrotime() == 4095_u16np);

    event = bh_spc_event_from_u8({0, 0x0f, 0, 0});
    CHECK(event.macrotime() == 0xf00_u16np);

    event = bh_spc_event_from_u8({0, 0xf0, 0xff, 0xff});
    CHECK(event.macrotime() == 0_u16np);
}

TEST_CASE("flags", "[bh_spc_event]") {
    bh_spc_event event{};

    event = bh_spc_event_from_u8({0, 0, 0, 0});
    CHECK_FALSE(event.invalid_flag());
    CHECK_FALSE(event.macrotime_overflow_flag());
    CHECK_FALSE(event.gap_flag());
    CHECK_FALSE(event.marker_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 7});
    CHECK(event.invalid_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 6});
    CHECK(event.macrotime_overflow_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 5});
    CHECK(event.gap_flag());

    event = bh_spc_event_from_u8({0, 0, 0, 1 << 4});
    CHECK(event.marker_flag());
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
    CHECK(event.multiple_macrotime_overflow_count() == 0_u32np);

    event = bh_spc_event_from_u8({1, 0, 0, 0});
    CHECK(event.multiple_macrotime_overflow_count() == 1_u32np);

    event = bh_spc_event_from_u8({0x80, 0, 0, 0});
    CHECK(event.multiple_macrotime_overflow_count() == 128_u32np);

    event = bh_spc_event_from_u8({0, 1, 0, 0});
    CHECK(event.multiple_macrotime_overflow_count() == 256_u32np);

    event = bh_spc_event_from_u8({0, 0x80, 0, 0});
    CHECK(event.multiple_macrotime_overflow_count() == 32768_u32np);

    event = bh_spc_event_from_u8({0, 0, 1, 0});
    CHECK(event.multiple_macrotime_overflow_count() == 65536_u32np);

    event = bh_spc_event_from_u8({0, 0, 0x80, 0});
    CHECK(event.multiple_macrotime_overflow_count() == 8388608_u32np);

    event = bh_spc_event_from_u8({0, 0, 0, 1});
    CHECK(event.multiple_macrotime_overflow_count() == 16777216_u32np);

    event = bh_spc_event_from_u8({0, 0, 0, 0x08});
    CHECK(event.multiple_macrotime_overflow_count() == 134217728_u32np);
}

} // namespace tcspc
