/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/bh_spc.hpp"

#include "flimevt/discard.hpp"
#include "flimevt/event_set.hpp"

#include <catch2/catch.hpp>

#include <cstring>

using namespace flimevt;

TEST_CASE("ADCValue", "[bh_spc_event]") {
    union {
        bh_spc_event event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.get_adc_value() == 0);

    u.bytes[2] = 0xff;
    REQUIRE(u.event.get_adc_value() == 0xff);

    u.bytes[3] = 0x0f;
    REQUIRE(u.event.get_adc_value() == 4095);

    u.bytes[2] = 0;
    REQUIRE(u.event.get_adc_value() == 0xf00);

    u.bytes[0] = 0xff;
    u.bytes[1] = 0xff;
    u.bytes[3] = 0xf0;
    REQUIRE(u.event.get_adc_value() == 0);
}

TEST_CASE("RoutingSignals", "[bh_spc_event]") {
    union {
        bh_spc_event event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.get_routing_signals() == 0);
    REQUIRE(u.event.get_marker_bits() == 0);

    u.bytes[1] = 0x10;
    REQUIRE(u.event.get_routing_signals() == 1);
    REQUIRE(u.event.get_marker_bits() == 1);
    u.bytes[1] = 0x20;
    REQUIRE(u.event.get_routing_signals() == 2);
    REQUIRE(u.event.get_marker_bits() == 2);
    u.bytes[1] = 0x40;
    REQUIRE(u.event.get_routing_signals() == 4);
    REQUIRE(u.event.get_marker_bits() == 4);
    u.bytes[1] = 0x80;
    REQUIRE(u.event.get_routing_signals() == 8);
    REQUIRE(u.event.get_marker_bits() == 8);

    u.bytes[0] = u.bytes[2] = u.bytes[3] = 0xff;
    u.bytes[1] = 0x0f;
    REQUIRE(u.event.get_routing_signals() == 0);
    REQUIRE(u.event.get_marker_bits() == 0);
}

TEST_CASE("Macrotime", "[bh_spc_event]") {
    REQUIRE(bh_spc_event::macrotime_overflow_period == 4096);

    union {
        bh_spc_event event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.get_macrotime() == 0);

    u.bytes[0] = 0xff;
    REQUIRE(u.event.get_macrotime() == 0xff);

    u.bytes[1] = 0x0f;
    REQUIRE(u.event.get_macrotime() == 4095);

    u.bytes[0] = 0;
    REQUIRE(u.event.get_macrotime() == 0xf00);

    u.bytes[1] = 0xf0;
    u.bytes[2] = 0xff;
    u.bytes[3] = 0xff;
    REQUIRE(u.event.get_macrotime() == 0);
}

TEST_CASE("Flags", "[bh_spc_event]") {
    union {
        bh_spc_event event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(!u.event.get_invalid_flag());
    REQUIRE(!u.event.get_macrotime_overflow_flag());
    REQUIRE(!u.event.get_gap_flag());
    REQUIRE(!u.event.get_marker_flag());

    u.bytes[3] = 1 << 7;
    REQUIRE(u.event.get_invalid_flag());
    u.bytes[3] = 1 << 6;
    REQUIRE(u.event.get_macrotime_overflow_flag());
    u.bytes[3] = 1 << 5;
    REQUIRE(u.event.get_gap_flag());
    u.bytes[3] = 1 << 4;
    REQUIRE(u.event.get_marker_flag());
}

TEST_CASE("MacrotimeOverflow", "[bh_spc_event]") {
    union {
        bh_spc_event event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    // The GAP flag is orthogonal to macrotime overflow. Test all combinations
    // of the other 3 flags. (Although it is expected that INVALID is always
    // set when MARK is set.)
    std::uint8_t const INVALID = 1 << 7;
    std::uint8_t const MTOV = 1 << 6;
    std::uint8_t const MARK = 1 << 4;

    u.bytes[3] = 0; // Valid photon, no overflow
    REQUIRE(!u.event.is_multiple_macrotime_overflow());
    u.bytes[3] = MARK; // Mark, no overflow (not expected)
    REQUIRE(!u.event.is_multiple_macrotime_overflow());
    u.bytes[3] = MTOV; // Valid photon, single overflow
    REQUIRE(!u.event.is_multiple_macrotime_overflow());
    u.bytes[3] = MTOV | MARK; // Marker, single overflow (not expected)
    REQUIRE(!u.event.is_multiple_macrotime_overflow());
    u.bytes[3] = INVALID; // Invalid photon, no overflow
    REQUIRE(!u.event.is_multiple_macrotime_overflow());
    u.bytes[3] = INVALID | MARK; // Mark, no overflow
    REQUIRE(!u.event.is_multiple_macrotime_overflow());
    u.bytes[3] = INVALID | MTOV; // Multiple overflow
    REQUIRE(u.event.is_multiple_macrotime_overflow());
    u.bytes[3] = INVALID | MTOV | MARK; // Marker, single overflow
    REQUIRE(!u.event.is_multiple_macrotime_overflow());
}

TEST_CASE("MacrotimeOverflowCount", "[bh_spc_event]") {
    union {
        bh_spc_event event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 0);

    u.bytes[0] = 1;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 1);
    u.bytes[0] = 0x80;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 128);
    u.bytes[0] = 0;

    u.bytes[1] = 1;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 256);
    u.bytes[1] = 0x80;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 32768);
    u.bytes[1] = 0;

    u.bytes[2] = 1;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 65536);
    u.bytes[2] = 0x80;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 8388608);
    u.bytes[2] = 0;

    u.bytes[3] = 1;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 16777216);
    u.bytes[3] = 0x08;
    REQUIRE(u.event.get_multiple_macrotime_overflow_count() == 134217728);
    u.bytes[3] = 0;
}
