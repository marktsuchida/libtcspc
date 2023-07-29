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

static_assert(std::is_pod_v<bh_spc_event>);
static_assert(std::is_pod_v<bh_spc600_256ch_event>);
static_assert(std::is_pod_v<bh_spc600_4096ch_event>);

static_assert(sizeof(bh_spc_event) == 4);
static_assert(sizeof(bh_spc600_256ch_event) == 4);
static_assert(sizeof(bh_spc600_4096ch_event) == 6);

namespace {

template <typename BHSPCEvent>
inline auto
u8event(std::array<std::uint8_t, sizeof(BHSPCEvent)> bytes) noexcept
    -> BHSPCEvent {
    BHSPCEvent ret{};
    std::memcpy(&ret, bytes.data(), sizeof(ret));
    return ret;
}

} // namespace

TEST_CASE("bh_spc_event equality and inequality", "[bh_spc_event]") {
    CHECK(u8event<bh_spc_event>({1, 2, 3, 4}) ==
          u8event<bh_spc_event>({1, 2, 3, 4}));
    CHECK(u8event<bh_spc600_256ch_event>({1, 2, 3, 4}) ==
          u8event<bh_spc600_256ch_event>({1, 2, 3, 4}));
    CHECK(u8event<bh_spc600_4096ch_event>({1, 2, 3, 4, 5, 6}) ==
          u8event<bh_spc600_4096ch_event>({1, 2, 3, 4, 5, 6}));

    CHECK(u8event<bh_spc_event>({0, 0, 0, 1}) !=
          u8event<bh_spc_event>({0, 0, 0, 0}));
    CHECK(u8event<bh_spc600_256ch_event>({0, 0, 0, 1}) !=
          u8event<bh_spc600_256ch_event>({0, 0, 0, 0}));
    CHECK(u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 1}) !=
          u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}));

    CHECK(u8event<bh_spc_event>({1, 0, 0, 0}) !=
          u8event<bh_spc_event>({0, 0, 0, 0}));
    CHECK(u8event<bh_spc600_256ch_event>({1, 0, 0, 0}) !=
          u8event<bh_spc600_256ch_event>({0, 0, 0, 0}));
    CHECK(u8event<bh_spc600_4096ch_event>({1, 0, 0, 0, 0, 0}) !=
          u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}));
}

TEST_CASE("read bh_spc adc_value", "[bh_spc_event]") {
    SECTION("zero") {
        CHECK(u8event<bh_spc_event>({0, 0, 0, 0}).adc_value() == 0_u16np);

        CHECK(u8event<bh_spc600_256ch_event>({0, 0, 0, 0}).adc_value() ==
              0_u16np);

        CHECK(
            u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}).adc_value() ==
            0_u16np);
    }

    SECTION("max") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b0000'1111})
                  .adc_value() == 4095_u16np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1111'1111, 0b0000'0000, 0b0000'0000, 0b0000'0000})
                  .adc_value() == 255_u16np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b0000'1111,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .adc_value() == 4095_u16np);
    }

    SECTION("not affected by other bits") {
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b1111'1111, 0b0000'0000, 0b1111'0000})
                  .adc_value() == 0_u16np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b1111'1111, 0b1111'1111, 0b1111'1111})
                  .adc_value() == 0_u16np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b1111'0000,
                                               0b1111'1111, 0b1111'1111,
                                               0b1111'1111, 0b1111'1111})
                  .adc_value() == 0_u16np);
    }

    SECTION("bytes and nibbles map correctly") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'1111})
                  .adc_value() == 15_u16np << 8);
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b0000'0000})
                  .adc_value() == 255_u16np << 0);
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b1001'1011, 0b0000'1101})
                  .adc_value() == 0b1101'1001'1011_u16np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1001'1011, 0b0000'0000, 0b0000'0000, 0b0000'0000})
                  .adc_value() == 0b1001'1011_u16np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'1111,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .adc_value() == 15_u16np << 8);
        CHECK(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .adc_value() == 255_u16np << 0);
        CHECK(u8event<bh_spc600_4096ch_event>({0b1001'1011, 0b0000'1101,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .adc_value() == 0b1101'1001'1011_u16np);
    }
}

TEST_CASE("read bh_spc routing_signals and marker_bits", "[bh_spc_event]") {
    SECTION("zero") {
        CHECK(u8event<bh_spc_event>({0, 0, 0, 0}).routing_signals() == 0_u8np);
        CHECK(u8event<bh_spc_event>({0, 0, 0, 0}).marker_bits() == 0_u8np);

        CHECK(u8event<bh_spc600_256ch_event>({0, 0, 0, 0}).routing_signals() ==
              0_u8np);
        CHECK(u8event<bh_spc600_256ch_event>({0, 0, 0, 0}).marker_bits() ==
              0_u8np);

        CHECK(u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0})
                  .routing_signals() == 0_u8np);
        CHECK(u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0})
                  .marker_bits() == 0_u8np);
    }

    SECTION("max") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b1111'0000, 0b0000'0000, 0b0000'0000})
                  .routing_signals() == 15_u8np);
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b1111'0000, 0b0000'0000, 0b0000'0000})
                  .marker_bits() == 15_u8np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'1110})
                  .routing_signals() == 7_u8np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b1111'1111,
                                               0b0000'0000, 0b0000'0000})
                  .routing_signals() == 255_u8np);
    }

    SECTION("not affected by other bits") {
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b0000'1111, 0b1111'1111, 0b1111'1111})
                  .routing_signals() == 0_u8np);
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b0000'1111, 0b1111'1111, 0b1111'1111})
                  .marker_bits() == 0_u8np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'0001})
                  .routing_signals() == 0_u8np);
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111})
                  .marker_bits() == 0_u8np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1111'1111,
                                               0b1111'1111, 0b0000'0000,
                                               0b1111'1111, 0b1111'1111})
                  .routing_signals() == 0_u8np);
        CHECK(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1111'1111,
                                               0b1111'1111, 0b1111'1111,
                                               0b1111'1111, 0b1111'1111})
                  .marker_bits() == 0_u8np);
    }

    SECTION("maps to value") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b1101'0000, 0b0000'0000, 0b0000'0000})
                  .routing_signals() == 0b1101_u8np);
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b1101'0000, 0b0000'0000, 0b0000'0000})
                  .marker_bits() == 0b1101_u8np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'1010})
                  .routing_signals() == 0b101_u8np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b1101'1011,
                                               0b0000'0000, 0b0000'0000})
                  .routing_signals() == 0b1101'1011_u8np);
    }
}

TEST_CASE("read bh_spc macrotime", "[bh_spc_event]") {
    static_assert(bh_spc_event::macrotime_overflow_period == 4096);
    static_assert(bh_spc600_256ch_event::macrotime_overflow_period == 131'072);
    static_assert(bh_spc600_4096ch_event::macrotime_overflow_period ==
                  16'777'216);

    SECTION("zero") {
        CHECK(u8event<bh_spc_event>({0, 0, 0, 0}).macrotime() == 0_u16np);

        CHECK(u8event<bh_spc600_256ch_event>({0, 0, 0, 0}).macrotime() ==
              0_u32np);

        CHECK(
            u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}).macrotime() ==
            0_u32np);
    }

    SECTION("max") {
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b0000'1111, 0b0000'0000, 0b0000'0000})
                  .macrotime() == 4095_u16np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b1111'1111, 0b1111'1111, 0b0000'0001})
                  .macrotime() == 131'071_u32np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                               0b1111'1111, 0b0000'0000,
                                               0b1111'1111, 0b1111'1111})
                  .macrotime() == 16'777'215_u32np);
    }

    SECTION("not affected by other bits") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b1111'0000, 0b1111'1111, 0b1111'1111})
                  .macrotime() == 0_u16np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1111'1111, 0b0000'0000, 0b0000'0000, 0b1111'1110})
                  .macrotime() == 0_u32np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1111'1111,
                                               0b0000'0000, 0b1111'1111,
                                               0b0000'0000, 0b0000'0000})
                  .macrotime() == 0_u32np);
    }

    SECTION("bytes and nibbles map correctly") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'1111, 0b0000'0000, 0b0000'0000})
                  .macrotime() == 15_u16np << 8);
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b0000'0000, 0b0000'0000, 0b0000'0000})
                  .macrotime() == 255_u16np << 0);
        CHECK(u8event<bh_spc_event>(
                  {0b1001'1011, 0b0000'1101, 0b0000'0000, 0b0000'0000})
                  .macrotime() == 0b1101'1001'1011_u16np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001})
                  .macrotime() == 1_u32np << 16);
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b0000'0000})
                  .macrotime() == 255_u32np << 8);
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b1111'1111, 0b0000'0000, 0b0000'0000})
                  .macrotime() == 255_u32np << 0);
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b1001'1011, 0b1101'0011, 0b0000'0001})
                  .macrotime() == 0b1'1101'0011'1001'1011_u32np);

        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b1111'1111})
                  .macrotime() == 255_u32np << 8);
        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000,
                                               0b1111'1111, 0b0000'0000})
                  .macrotime() == 255_u32np << 0);
        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                               0b1111'1111, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .macrotime() == 255_u32np << 16);
        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                               0b1001'1011, 0b0000'0000,
                                               0b1101'0011, 0b1100'0110})
                  .macrotime() == 0b1001'1011'1100'0110'1101'0011_u32np);
    }
}

TEST_CASE("read bh_spc flags", "[bh_spc_event]") {
    SECTION("zero") {
        CHECK_FALSE(u8event<bh_spc_event>({0, 0, 0, 0}).invalid_flag());
        CHECK_FALSE(
            u8event<bh_spc_event>({0, 0, 0, 0}).macrotime_overflow_flag());
        CHECK_FALSE(u8event<bh_spc_event>({0, 0, 0, 0}).gap_flag());
        CHECK_FALSE(u8event<bh_spc_event>({0, 0, 0, 0}).marker_flag());

        CHECK_FALSE(
            u8event<bh_spc600_256ch_event>({0, 0, 0, 0}).invalid_flag());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>({0, 0, 0, 0})
                        .macrotime_overflow_flag());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>({0, 0, 0, 0}).gap_flag());
        CHECK_FALSE(
            u8event<bh_spc600_256ch_event>({0, 0, 0, 0}).marker_flag());

        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0})
                        .invalid_flag());
        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0})
                        .macrotime_overflow_flag());
        CHECK_FALSE(
            u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}).gap_flag());
        CHECK_FALSE(
            u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}).marker_flag());
    }

    SECTION("flag set") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1000'0000})
                  .invalid_flag());
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0100'0000})
                  .macrotime_overflow_flag());
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0010'0000})
                  .gap_flag());
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0001'0000})
                  .marker_flag());

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1000'0000})
                  .invalid_flag());
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0100'0000})
                  .macrotime_overflow_flag());
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0010'0000})
                  .gap_flag());

        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0001'0000,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .invalid_flag());
        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0010'0000,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .macrotime_overflow_flag());
        CHECK(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0100'0000,
                                               0b0000'0000, 0b0000'0000,
                                               0b0000'0000, 0b0000'0000})
                  .gap_flag());
    }

    SECTION("not affected by other bits") {
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0111'1111})
                        .invalid_flag());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1011'1111})
                        .macrotime_overflow_flag());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1101'1111})
                        .gap_flag());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1110'1111})
                        .marker_flag());

        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0111'1111})
                        .invalid_flag());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1011'1111})
                        .macrotime_overflow_flag());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1101'1111})
                        .gap_flag());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111})
                        .marker_flag());

        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1110'1111,
                                                     0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111})
                        .invalid_flag());
        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1101'1111,
                                                     0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111})
                        .macrotime_overflow_flag());
        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1011'1111,
                                                     0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111})
                        .gap_flag());
        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111})
                        .marker_flag());
    }
}

TEST_CASE("read bh_spc multiple_macrotime_overflow_count", "[bh_spc_event]") {
    SECTION("zero") {
        CHECK(u8event<bh_spc_event>({0, 0, 0, 0})
                  .multiple_macrotime_overflow_count() == 0_u32np);

        CHECK(u8event<bh_spc600_256ch_event>({0, 0, 0, 0})
                  .multiple_macrotime_overflow_count() == 0_u32np);

        CHECK(u8event<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0})
                  .multiple_macrotime_overflow_count() == 0_u32np);
    }

    SECTION("max") {
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0000'1111})
                  .multiple_macrotime_overflow_count() == 268'435'455_u32np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0000'1111})
                  .multiple_macrotime_overflow_count() == 268'435'455_u32np);
    }

    SECTION("not affected by other bits") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1111'0000})
                  .multiple_macrotime_overflow_count() == 0_u32np);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1111'0000})
                  .multiple_macrotime_overflow_count() == 0_u32np);

        CHECK(u8event<bh_spc600_4096ch_event>(
                  {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111})
                  .multiple_macrotime_overflow_count() == 0_u32np);
    }

    SECTION("bytes and nibbles map correctly") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'1111})
                  .multiple_macrotime_overflow_count() == 15_u32np << 24);
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b0000'0000})
                  .multiple_macrotime_overflow_count() == 255_u32np << 16);
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b1111'1111, 0b0000'0000, 0b0000'0000})
                  .multiple_macrotime_overflow_count() == 255_u32np << 8);
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b0000'0000, 0b0000'0000, 0b0000'0000})
                  .multiple_macrotime_overflow_count() == 255_u32np << 0);

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'1111})
                  .multiple_macrotime_overflow_count() == 15_u32np << 24);
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b0000'0000})
                  .multiple_macrotime_overflow_count() == 255_u32np << 16);
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b1111'1111, 0b0000'0000, 0b0000'0000})
                  .multiple_macrotime_overflow_count() == 255_u32np << 8);
        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1111'1111, 0b0000'0000, 0b0000'0000, 0b0000'0000})
                  .multiple_macrotime_overflow_count() == 255_u32np << 0);
    }
}

TEST_CASE("bh_spc is_multiple_macrotime_overflow", "[bh_spc_event]") {
    // Test all combinations of INV/MTOV/MARK. GAP is unrelated.

    SECTION("unrelated bits cleared") {
        CHECK(u8event<bh_spc_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1100'0000})
                  .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1101'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1000'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1001'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0100'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0101'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0001'0000})
                        .is_multiple_macrotime_overflow());

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1100'0000})
                  .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1000'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0100'0000})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000})
                        .is_multiple_macrotime_overflow());

        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0b0000'0000, 0b0000'0000,
                                                     0b0000'0000, 0b0000'0000,
                                                     0b0000'0000, 0b0000'0000})
                        .is_multiple_macrotime_overflow());
    }

    SECTION("unrelated bits set") {
        CHECK(u8event<bh_spc_event>(
                  {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1110'1111})
                  .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1010'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1011'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0110'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0111'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0010'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0011'1111})
                        .is_multiple_macrotime_overflow());

        CHECK(u8event<bh_spc600_256ch_event>(
                  {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111})
                  .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1011'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0111'1111})
                        .is_multiple_macrotime_overflow());
        CHECK_FALSE(u8event<bh_spc600_256ch_event>(
                        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0011'1111})
                        .is_multiple_macrotime_overflow());

        CHECK_FALSE(u8event<bh_spc600_4096ch_event>({0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111})
                        .is_multiple_macrotime_overflow());
    }
}

} // namespace tcspc
