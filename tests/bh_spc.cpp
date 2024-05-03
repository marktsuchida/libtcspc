/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/bh_spc.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/data_types.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/npint.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <type_traits>

namespace tcspc {

static_assert(std::is_pod_v<bh_spc_event>);
static_assert(std::is_pod_v<bh_spc600_256ch_event>);
static_assert(std::is_pod_v<bh_spc600_4096ch_event>);

static_assert(sizeof(bh_spc_event) == 4);
static_assert(sizeof(bh_spc600_256ch_event) == 4);
static_assert(sizeof(bh_spc600_4096ch_event) == 6);

TEST_CASE("bh spc equality and inequality") {
    CHECK(from_reversed_bytes<bh_spc_event>({1, 2, 3, 4}) ==
          from_reversed_bytes<bh_spc_event>({1, 2, 3, 4}));
    CHECK(from_reversed_bytes<bh_spc600_256ch_event>({1, 2, 3, 4}) ==
          from_reversed_bytes<bh_spc600_256ch_event>({1, 2, 3, 4}));
    CHECK(from_reversed_bytes<bh_spc600_4096ch_event>({1, 2, 3, 4, 5, 6}) ==
          from_reversed_bytes<bh_spc600_4096ch_event>({1, 2, 3, 4, 5, 6}));

    CHECK(from_reversed_bytes<bh_spc_event>({0, 0, 0, 1}) !=
          from_reversed_bytes<bh_spc_event>({0, 0, 0, 0}));
    CHECK(from_reversed_bytes<bh_spc600_256ch_event>({0, 0, 0, 1}) !=
          from_reversed_bytes<bh_spc600_256ch_event>({0, 0, 0, 0}));
    CHECK(from_reversed_bytes<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 1}) !=
          from_reversed_bytes<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}));

    CHECK(from_reversed_bytes<bh_spc_event>({1, 0, 0, 0}) !=
          from_reversed_bytes<bh_spc_event>({0, 0, 0, 0}));
    CHECK(from_reversed_bytes<bh_spc600_256ch_event>({1, 0, 0, 0}) !=
          from_reversed_bytes<bh_spc600_256ch_event>({0, 0, 0, 0}));
    CHECK(from_reversed_bytes<bh_spc600_4096ch_event>({1, 0, 0, 0, 0, 0}) !=
          from_reversed_bytes<bh_spc600_4096ch_event>({0, 0, 0, 0, 0, 0}));
}

// Note that the bit patterns of 'from_reversed_bytes' are in reverse byte
// order compared to BH documentation. But this way most of the integer fields
// greater than 8 bits are contiguous.

TEST_CASE("bh spc event type") {
    auto const zero = from_reversed_bytes<bh_spc_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(zero.gap_flag());
    CHECK_FALSE(zero.invalid_flag());
    CHECK_FALSE(zero.macrotime_overflow_flag());
    CHECK_FALSE(zero.marker_flag());
    CHECK_FALSE(zero.is_multiple_macrotime_overflow());

    auto const non_flag_bits = from_reversed_bytes<bh_spc_event>(
        {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK_FALSE(non_flag_bits.gap_flag());
    CHECK_FALSE(non_flag_bits.invalid_flag());
    CHECK_FALSE(non_flag_bits.macrotime_overflow_flag());
    CHECK_FALSE(non_flag_bits.marker_flag());
    CHECK_FALSE(non_flag_bits.is_multiple_macrotime_overflow());

    auto const inv = from_reversed_bytes<bh_spc_event>(
        {0b1000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(inv.invalid_flag());
    CHECK_FALSE(inv.macrotime_overflow_flag());
    CHECK_FALSE(inv.gap_flag());
    CHECK_FALSE(inv.marker_flag());
    CHECK_FALSE(inv.is_multiple_macrotime_overflow());

    auto const mtov = from_reversed_bytes<bh_spc_event>(
        {0b0100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(mtov.invalid_flag());
    CHECK(mtov.macrotime_overflow_flag());
    CHECK_FALSE(mtov.gap_flag());
    CHECK_FALSE(mtov.marker_flag());
    CHECK_FALSE(mtov.is_multiple_macrotime_overflow());

    auto const gap = from_reversed_bytes<bh_spc_event>(
        {0b0010'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(gap.invalid_flag());
    CHECK_FALSE(gap.macrotime_overflow_flag());
    CHECK(gap.gap_flag());
    CHECK_FALSE(gap.marker_flag());
    CHECK_FALSE(gap.is_multiple_macrotime_overflow());

    auto const mark = from_reversed_bytes<bh_spc_event>(
        {0b1001'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(mark.invalid_flag());
    CHECK_FALSE(mark.macrotime_overflow_flag());
    CHECK_FALSE(mark.gap_flag());
    CHECK(mark.marker_flag());
    CHECK_FALSE(mark.is_multiple_macrotime_overflow());

    auto const multi_mtov = from_reversed_bytes<bh_spc_event>(
        {0b1100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(multi_mtov.invalid_flag());
    CHECK(multi_mtov.macrotime_overflow_flag());
    CHECK_FALSE(multi_mtov.gap_flag());
    CHECK_FALSE(multi_mtov.marker_flag());
    CHECK(multi_mtov.is_multiple_macrotime_overflow());

    auto const mark_mtov = from_reversed_bytes<bh_spc_event>(
        {0b1101'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(mark_mtov.invalid_flag());
    CHECK(mark_mtov.macrotime_overflow_flag());
    CHECK_FALSE(mark_mtov.gap_flag());
    CHECK(mark_mtov.marker_flag());
    CHECK_FALSE(mark_mtov.is_multiple_macrotime_overflow());
}

TEST_CASE("bh spc600 256ch event type") {
    auto const zero = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(zero.gap_flag());
    CHECK_FALSE(zero.invalid_flag());
    CHECK_FALSE(zero.macrotime_overflow_flag());
    CHECK_FALSE(zero.marker_flag());
    CHECK_FALSE(zero.is_multiple_macrotime_overflow());

    auto const non_flag_bits = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0001'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK_FALSE(non_flag_bits.gap_flag());
    CHECK_FALSE(non_flag_bits.invalid_flag());
    CHECK_FALSE(non_flag_bits.macrotime_overflow_flag());
    CHECK_FALSE(non_flag_bits.marker_flag());
    CHECK_FALSE(non_flag_bits.is_multiple_macrotime_overflow());

    auto const inv = from_reversed_bytes<bh_spc_event>(
        {0b1000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(inv.invalid_flag());
    CHECK_FALSE(inv.macrotime_overflow_flag());
    CHECK_FALSE(inv.gap_flag());
    CHECK_FALSE(inv.marker_flag());
    CHECK_FALSE(inv.is_multiple_macrotime_overflow());

    auto const mtov = from_reversed_bytes<bh_spc_event>(
        {0b0100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(mtov.invalid_flag());
    CHECK(mtov.macrotime_overflow_flag());
    CHECK_FALSE(mtov.gap_flag());
    CHECK_FALSE(mtov.marker_flag());
    CHECK_FALSE(mtov.is_multiple_macrotime_overflow());

    auto const gap = from_reversed_bytes<bh_spc_event>(
        {0b0010'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(gap.invalid_flag());
    CHECK_FALSE(gap.macrotime_overflow_flag());
    CHECK(gap.gap_flag());
    CHECK_FALSE(gap.marker_flag());
    CHECK_FALSE(gap.is_multiple_macrotime_overflow());

    auto const multi_mtov = from_reversed_bytes<bh_spc_event>(
        {0b1100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(multi_mtov.invalid_flag());
    CHECK(multi_mtov.macrotime_overflow_flag());
    CHECK_FALSE(multi_mtov.gap_flag());
    CHECK_FALSE(multi_mtov.marker_flag());
    CHECK(multi_mtov.is_multiple_macrotime_overflow());
}

TEST_CASE("bh spc600 4096ch event type") {
    auto const zero = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000,
         0b0000'0000});
    CHECK_FALSE(zero.gap_flag());
    CHECK_FALSE(zero.invalid_flag());
    CHECK_FALSE(zero.macrotime_overflow_flag());
    CHECK_FALSE(zero.marker_flag());
    CHECK_FALSE(zero.is_multiple_macrotime_overflow());

    auto const non_flag_bits = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1000'1111,
         0b1111'1111});
    CHECK_FALSE(non_flag_bits.gap_flag());
    CHECK_FALSE(non_flag_bits.invalid_flag());
    CHECK_FALSE(non_flag_bits.macrotime_overflow_flag());
    CHECK_FALSE(non_flag_bits.marker_flag());
    CHECK_FALSE(non_flag_bits.is_multiple_macrotime_overflow());

    auto const inv = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0001'0000,
         0b0000'0000});
    CHECK_FALSE(inv.gap_flag());
    CHECK(inv.invalid_flag());
    CHECK_FALSE(inv.macrotime_overflow_flag());
    CHECK_FALSE(inv.marker_flag());
    CHECK_FALSE(inv.is_multiple_macrotime_overflow());

    auto const mtov = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0010'0000,
         0b0000'0000});
    CHECK_FALSE(mtov.gap_flag());
    CHECK_FALSE(mtov.invalid_flag());
    CHECK(mtov.macrotime_overflow_flag());
    CHECK_FALSE(mtov.marker_flag());
    CHECK_FALSE(mtov.is_multiple_macrotime_overflow());

    auto const gap = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0100'0000,
         0b0000'0000});
    CHECK(gap.gap_flag());
    CHECK_FALSE(gap.invalid_flag());
    CHECK_FALSE(gap.macrotime_overflow_flag());
    CHECK_FALSE(gap.marker_flag());
    CHECK_FALSE(gap.is_multiple_macrotime_overflow());

    // No multiple macrotime overflow.
    auto const inv_mtov = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0011'0000,
         0b0000'0000});
    CHECK_FALSE(inv_mtov.gap_flag());
    CHECK(inv_mtov.invalid_flag());
    CHECK(inv_mtov.macrotime_overflow_flag());
    CHECK_FALSE(inv_mtov.marker_flag());
    CHECK_FALSE(inv_mtov.is_multiple_macrotime_overflow());
}

TEST_CASE("bh spc read adc count") {
    auto const adc0 = from_reversed_bytes<bh_spc_event>(
        {0b1111'0000, 0b0000'0000, 0b1111'1111, 0b1111'1111});
    CHECK(adc0.adc_value() == 0_u16np);

    auto const adc1 = from_reversed_bytes<bh_spc_event>(
        {0b0000'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000});
    CHECK(adc1.adc_value() == 1_u16np);

    auto const adc256 = from_reversed_bytes<bh_spc_event>(
        {0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(adc256.adc_value() == 256_u16np);

    auto const adc_max = from_reversed_bytes<bh_spc_event>(
        {0b0000'1111, 0b1111'1111, 0b0000'0000, 0b0000'0000});
    CHECK(adc_max.adc_value() == 4095_u16np);
}

TEST_CASE("bh spc600 256ch read adc count") {
    auto const adc0 = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0000'0000});
    CHECK(adc0.adc_value() == 0_u16np);

    auto const adc_max = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b1111'1111});
    CHECK(adc_max.adc_value() == 255_u16np);
}

TEST_CASE("bh spc600 4096ch read adc count") {
    auto const adc0 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0000'0000,
         0b0000'0000});
    CHECK(adc0.adc_value() == 0_u16np);

    auto const adc1 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000,
         0b0000'0001});
    CHECK(adc1.adc_value() == 1_u16np);

    auto const adc256 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001,
         0b0000'0000});
    CHECK(adc256.adc_value() == 256_u16np);

    auto const adc_max = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'1111,
         0b1111'1111});
    CHECK(adc_max.adc_value() == 4095_u16np);
}

TEST_CASE("bh spc read routing signals") {
    auto const rout0 = from_reversed_bytes<bh_spc_event>(
        {0b1111'1111, 0b1111'1111, 0b0000'1111, 0b1111'1111});
    CHECK(rout0.routing_signals() == 0_u8np);

    auto const rout_max = from_reversed_bytes<bh_spc_event>(
        {0b0000'0000, 0b0000'0000, 0b1111'0000, 0b0000'0000});
    CHECK(rout_max.routing_signals() == 15_u8np);
}

TEST_CASE("bh spc600 256ch read routing signals") {
    auto const rout0 = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b1111'0001, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK(rout0.routing_signals() == 0_u8np);

    auto const rout_max = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0000'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(rout_max.routing_signals() == 7_u8np);
}

TEST_CASE("bh spc600 4096ch read routing signals") {
    auto const rout0 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b1111'1111, 0b1111'1111, 0b0000'0000, 0b1111'1111, 0b1111'1111,
         0b1111'1111});
    CHECK(rout0.routing_signals() == 0_u8np);

    auto const rout_max = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b0000'0000, 0b0000'0000,
         0b0000'0000});
    CHECK(rout_max.routing_signals() == 255_u8np);
}

TEST_CASE("bh spc read macrotime") {
    auto const macrotime0 = from_reversed_bytes<bh_spc_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'0000, 0b0000'0000});
    CHECK(macrotime0.macrotime() == 0_u16np);

    auto const macrotime1 = from_reversed_bytes<bh_spc_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001});
    CHECK(macrotime1.macrotime() == 1_u16np);

    auto const macrotime256 = from_reversed_bytes<bh_spc_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0001, 0b0000'0000});
    CHECK(macrotime256.macrotime() == 256_u16np);

    auto const macrotime_max = from_reversed_bytes<bh_spc_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'1111, 0b1111'1111});
    CHECK(macrotime_max.macrotime() == 4095_u16np);
}

TEST_CASE("bh spc600 256ch read macrotime") {
    auto const macrotime0 = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b1111'1111});
    CHECK(macrotime0.macrotime() == 0_u32np);

    auto const macrotime1 = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0001, 0b0000'0000});
    CHECK(macrotime1.macrotime() == 1_u32np);

    auto const macrotime256 = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0000'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000});
    CHECK(macrotime256.macrotime() == 256_u32np);

    auto const macrotime65536 = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(macrotime65536.macrotime() == 65536_u32np);

    auto const macrotime_max = from_reversed_bytes<bh_spc600_256ch_event>(
        {0b0000'0001, 0b1111'1111, 0b1111'1111, 0b0000'0000});
    CHECK(macrotime_max.macrotime() == 131'071_u32np);
}

TEST_CASE("bh spc600 4096ch read macrotime") {
    auto const macrotime0 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b0000'0000, 0b1111'1111,
         0b1111'1111});
    CHECK(macrotime0.macrotime() == 0_u32np);

    auto const macrotime1 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000,
         0b0000'0000});
    CHECK(macrotime1.macrotime() == 1_u32np);

    auto const macrotime256 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000,
         0b0000'0000});
    CHECK(macrotime256.macrotime() == 256_u32np);

    auto const macrotime65536 = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001, 0b0000'0000,
         0b0000'0000});
    CHECK(macrotime65536.macrotime() == 65536_u32np);

    auto const macrotime_max = from_reversed_bytes<bh_spc600_4096ch_event>(
        {0b1111'1111, 0b1111'1111, 0b0000'0000, 0b1111'1111, 0b0000'0000,
         0b0000'0000});
    CHECK(macrotime_max.macrotime() == 16'777'215_u32np);
}

TEMPLATE_TEST_CASE("bh spc read multiple macrotime overflow count",
                   "[bh_spc_event]", bh_spc_event, bh_spc600_256ch_event) {
    auto const cnt0 = from_reversed_bytes<TestType>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(cnt0.multiple_macrotime_overflow_count() == 0_u32np);

    auto const cnt1 = from_reversed_bytes<TestType>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001});
    CHECK(cnt1.multiple_macrotime_overflow_count() == 1_u32np);

    auto const cnt256 = from_reversed_bytes<TestType>(
        {0b0000'0000, 0b0000'0000, 0b0000'0001, 0b0000'0000});
    CHECK(cnt256.multiple_macrotime_overflow_count() == 256_u32np);

    auto const cnt65536 = from_reversed_bytes<TestType>(
        {0b0000'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000});
    CHECK(cnt65536.multiple_macrotime_overflow_count() == 65536_u32np);

    auto const cnt16777216 = from_reversed_bytes<TestType>(
        {0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(cnt16777216.multiple_macrotime_overflow_count() == 16'777'216_u32np);

    auto const cnt_max = from_reversed_bytes<TestType>(
        {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK(cnt_max.multiple_macrotime_overflow_count() == 268'435'455_u32np);
}

TEST_CASE("bh spc assign") {
    CHECK(bh_spc_event::make_photon(0_u16np, 0_u16np, 0_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc_event::make_photon(1_u16np, 2_u16np, 3_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b0000'0000, 0b0000'0010, 0b0011'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_photon(1_u16np, 2_u16np, 3_u8np, true) ==
          from_reversed_bytes<bh_spc_event>(
              {0b0100'0000, 0b0000'0010, 0b0011'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_photon(4094_u16np, 4094_u16np, 14_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b0000'1111, 0b1111'1110, 0b1110'1111, 0b1111'1110}));
    CHECK(bh_spc_event::make_photon(4095_u16np, 4095_u16np, 15_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(bh_spc_event::make_invalid_photon(0_u16np, 0_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc_event::make_invalid_photon(1_u16np, 2_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1000'0000, 0b0000'0010, 0b0000'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_invalid_photon(4094_u16np, 4094_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1000'1111, 0b1111'1110, 0b0000'1111, 0b1111'1110}));
    CHECK(bh_spc_event::make_invalid_photon(4095_u16np, 4095_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1000'1111, 0b1111'1111, 0b0000'1111, 0b1111'1111}));

    CHECK(bh_spc_event::make_marker(0_u16np, 0_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc_event::make_marker(1_u16np, 2_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'0000, 0b0000'0000, 0b0010'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_marker(1_u16np, 2_u8np, true) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1101'0000, 0b0000'0000, 0b0010'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_marker(4094_u16np, 14_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'0000, 0b0000'0000, 0b1110'1111, 0b1111'1110}));
    CHECK(bh_spc_event::make_marker(4095_u16np, 15_u8np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'0000, 0b0000'0000, 0b1111'1111, 0b1111'1111}));

    CHECK(bh_spc_event::make_marker0_with_intensity_count(0_u16np, 1_u8np,
                                                          0_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'0000, 0b0000'0000, 0b0001'0000, 0b0000'0000}));
    CHECK(bh_spc_event::make_marker0_with_intensity_count(1_u16np, 3_u8np,
                                                          2_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'0000, 0b0000'0010, 0b0011'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_marker0_with_intensity_count(1_u16np, 3_u8np,
                                                          2_u16np, true) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1101'0000, 0b0000'0010, 0b0011'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_marker0_with_intensity_count(4094_u16np, 13_u8np,
                                                          4093_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'1111, 0b1111'1101, 0b1101'1111, 0b1111'1110}));
    CHECK(bh_spc_event::make_marker0_with_intensity_count(4095_u16np, 15_u8np,
                                                          4095_u16np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1001'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(bh_spc_event::make_multiple_macrotime_overflow(0_u32np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc_event::make_multiple_macrotime_overflow(1_u32np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(bh_spc_event::make_multiple_macrotime_overflow(256_u32np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1100'0000, 0b0000'0000, 0b0000'0001, 0b0000'0000}));
    CHECK(bh_spc_event::make_multiple_macrotime_overflow(65536_u32np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1100'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc_event::make_multiple_macrotime_overflow(16'777'216_u32np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1100'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc_event::make_multiple_macrotime_overflow(268'435'455_u32np) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1100'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(bh_spc_event{}.gap_flag(true) ==
          from_reversed_bytes<bh_spc_event>(
              {0b0010'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));

    CHECK(from_reversed_bytes<bh_spc_event>(
              {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111})
              .gap_flag(false) ==
          from_reversed_bytes<bh_spc_event>(
              {0b1101'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));
}

TEST_CASE("bh spc600 256ch assign") {
    CHECK(bh_spc600_256ch_event::make_photon(0_u32np, 0_u8np, 0_u8np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc600_256ch_event::make_photon(1_u32np, 2_u8np, 3_u8np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b0000'0110, 0b0000'0000, 0b0000'0001, 0b0000'0010}));
    CHECK(bh_spc600_256ch_event::make_photon(1_u32np, 2_u8np, 3_u8np, true) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b0100'0110, 0b0000'0000, 0b0000'0001, 0b0000'0010}));
    CHECK(
        bh_spc600_256ch_event::make_photon(131'070_u32np, 254_u8np, 6_u8np) ==
        from_reversed_bytes<bh_spc600_256ch_event>(
            {0b0000'1101, 0b1111'1111, 0b1111'1110, 0b1111'1110}));
    CHECK(
        bh_spc600_256ch_event::make_photon(131'071_u32np, 255_u8np, 7_u8np) ==
        from_reversed_bytes<bh_spc600_256ch_event>(
            {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(bh_spc600_256ch_event::make_invalid_photon(0_u32np, 0_u8np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc600_256ch_event::make_invalid_photon(1_u32np, 2_u8np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1000'0000, 0b0000'0000, 0b0000'0001, 0b0000'0010}));
    CHECK(
        bh_spc600_256ch_event::make_invalid_photon(131'070_u32np, 254_u8np) ==
        from_reversed_bytes<bh_spc600_256ch_event>(
            {0b1000'0001, 0b1111'1111, 0b1111'1110, 0b1111'1110}));
    CHECK(
        bh_spc600_256ch_event::make_invalid_photon(131'071_u32np, 255_u8np) ==
        from_reversed_bytes<bh_spc600_256ch_event>(
            {0b1000'0001, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(bh_spc600_256ch_event::make_multiple_macrotime_overflow(0_u32np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc600_256ch_event::make_multiple_macrotime_overflow(1_u32np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1100'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(bh_spc600_256ch_event::make_multiple_macrotime_overflow(256_u32np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1100'0000, 0b0000'0000, 0b0000'0001, 0b0000'0000}));
    CHECK(
        bh_spc600_256ch_event::make_multiple_macrotime_overflow(65536_u32np) ==
        from_reversed_bytes<bh_spc600_256ch_event>(
            {0b1100'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc600_256ch_event::make_multiple_macrotime_overflow(
              16'777'216_u32np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1100'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(bh_spc600_256ch_event::make_multiple_macrotime_overflow(
              268'435'455_u32np) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1100'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(bh_spc600_256ch_event{}.gap_flag(true) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b0010'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));

    CHECK(from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111})
              .gap_flag(false) ==
          from_reversed_bytes<bh_spc600_256ch_event>(
              {0b1101'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));
}

TEST_CASE("bh spc600 4096ch assign") {
    CHECK(bh_spc600_4096ch_event::make_photon(0_u32np, 0_u16np, 0_u8np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000,
               0b0000'0000}));
    CHECK(bh_spc600_4096ch_event::make_photon(1_u32np, 2_u16np, 3_u8np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0000, 0b0000'0001, 0b0000'0011, 0b0000'0000, 0b0000'0000,
               0b0000'0010}));
    CHECK(
        bh_spc600_4096ch_event::make_photon(1_u32np, 2_u16np, 3_u8np, true) ==
        from_reversed_bytes<bh_spc600_4096ch_event>(
            {0b0000'0000, 0b0000'0001, 0b0000'0011, 0b0000'0000, 0b0010'0000,
             0b0000'0010}));
    CHECK(bh_spc600_4096ch_event::make_photon(256_u32np, 2_u16np, 3_u8np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0001, 0b0000'0000, 0b0000'0011, 0b0000'0000, 0b0000'0000,
               0b0000'0010}));
    CHECK(bh_spc600_4096ch_event::make_photon(65536_u32np, 2_u16np, 3_u8np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0011, 0b0000'0001, 0b0000'0000,
               0b0000'0010}));
    CHECK(bh_spc600_4096ch_event::make_photon(16'777'214_u32np, 4094_u16np,
                                              254_u8np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b1111'1111, 0b1111'1110, 0b1111'1110, 0b1111'1111, 0b0000'1111,
               0b1111'1110}));
    CHECK(bh_spc600_4096ch_event::make_photon(16'777'215_u32np, 4095_u16np,
                                              255_u8np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111, 0b0000'1111,
               0b1111'1111}));

    CHECK(bh_spc600_4096ch_event::make_invalid_photon(0_u32np, 0_u16np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0001'0000,
               0b0000'0000}));
    CHECK(bh_spc600_4096ch_event::make_invalid_photon(1_u32np, 2_u16np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0001'0000,
               0b0000'0010}));
    CHECK(
        bh_spc600_4096ch_event::make_invalid_photon(1_u32np, 2_u16np, true) ==
        from_reversed_bytes<bh_spc600_4096ch_event>(
            {0b0000'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0011'0000,
             0b0000'0010}));
    CHECK(bh_spc600_4096ch_event::make_invalid_photon(256_u32np, 2_u16np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0001, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0001'0000,
               0b0000'0010}));
    CHECK(bh_spc600_4096ch_event::make_invalid_photon(65536_u32np, 2_u16np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001, 0b0001'0000,
               0b0000'0010}));
    CHECK(bh_spc600_4096ch_event::make_invalid_photon(16'777'214_u32np,
                                                      4094_u16np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b1111'1111, 0b1111'1110, 0b0000'0000, 0b1111'1111, 0b0001'1111,
               0b1111'1110}));
    CHECK(bh_spc600_4096ch_event::make_invalid_photon(16'777'215_u32np,
                                                      4095_u16np) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b1111'1111, 0b1111'1111, 0b0000'0000, 0b1111'1111, 0b0001'1111,
               0b1111'1111}));

    CHECK(bh_spc600_4096ch_event{}.gap_flag(true) ==
          from_reversed_bytes<bh_spc600_4096ch_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0100'0000,
               0b0000'0000}));

    CHECK(
        from_reversed_bytes<bh_spc600_4096ch_event>({0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111,
                                                     0b1111'1111, 0b1111'1111})
            .gap_flag(false) == from_reversed_bytes<bh_spc600_4096ch_event>(
                                    {0b1111'1111, 0b1111'1111, 0b1111'1111,
                                     0b1111'1111, 0b1011'1111, 0b1111'1111}));
}

namespace {

struct bh_data_types : default_data_types {
    using channel_type = u8;
    using difftime_type = u16;
};

} // namespace

TEST_CASE("type constraints: decode_bh_spc") {
    using proc_type = decltype(decode_bh_spc<bh_data_types>(
        sink_events<time_reached_event<bh_data_types>,
                    time_correlated_detection_event<bh_data_types>,
                    marker_event<bh_data_types>,
                    data_lost_event<bh_data_types>, warning_event>()));

    STATIC_CHECK(is_processor_v<proc_type, bh_spc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bh_spc600_256ch_event>);
}

TEST_CASE("type constraints: decode_bh_spc_with_intensity_counter") {
    using proc_type =
        decltype(decode_bh_spc_with_intensity_counter<bh_data_types>(
            sink_events<time_reached_event<bh_data_types>,
                        time_correlated_detection_event<bh_data_types>,
                        marker_event<bh_data_types>,
                        bulk_counts_event<bh_data_types>,
                        data_lost_event<bh_data_types>, warning_event>()));

    STATIC_CHECK(is_processor_v<proc_type, bh_spc_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bh_spc600_256ch_event>);
}

TEST_CASE("type constraints: decode_bh_spc600_256ch") {
    using proc_type = decltype(decode_bh_spc600_256ch<bh_data_types>(
        sink_events<time_reached_event<bh_data_types>,
                    time_correlated_detection_event<bh_data_types>,
                    data_lost_event<bh_data_types>, warning_event>()));

    STATIC_CHECK(is_processor_v<proc_type, bh_spc600_256ch_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bh_spc600_4096ch_event>);
}

TEST_CASE("type constraints: decode_bh_spc600_4096ch") {
    using proc_type = decltype(decode_bh_spc600_4096ch<bh_data_types>(
        sink_events<time_reached_event<bh_data_types>,
                    time_correlated_detection_event<bh_data_types>,
                    data_lost_event<bh_data_types>, warning_event>()));

    STATIC_CHECK(is_processor_v<proc_type, bh_spc600_4096ch_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bh_spc600_256ch_event>);
}

TEST_CASE("introspect: bh_spc") {
    check_introspect_simple_processor(decode_bh_spc(null_sink()));
    check_introspect_simple_processor(
        decode_bh_spc_with_intensity_counter(null_sink()));
    check_introspect_simple_processor(decode_bh_spc600_256ch(null_sink()));
    check_introspect_simple_processor(decode_bh_spc600_4096ch(null_sink()));
}

TEST_CASE("decode bh spc") {
    using out_events =
        type_list<time_correlated_detection_event<>, marker_event<>,
                  time_reached_event<>, data_lost_event<>, warning_event>;

    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, decode_bh_spc(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    bool const gap = GENERATE(false, true);

    SECTION("photon, no mtov") {
        in.handle(bh_spc_event::make_photon(42_u16np, 123_u16np, 5_u8np, false)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(time_correlated_detection_event<>{42, 5, 123}));
    }

    SECTION("photon, mtov") {
        in.handle(bh_spc_event::make_photon(42_u16np, 123_u16np, 5_u8np, true)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{4096 + 42}));
        REQUIRE(
            out.check(time_correlated_detection_event<>{4096 + 42, 5, 123}));
    }

    SECTION("invalid photon") {
        in.handle(bh_spc_event::make_invalid_photon(42_u16np, 123_u16np)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
    }

    SECTION("marker, no mtov") {
        in.handle(
            bh_spc_event::make_marker(42_u16np, 5_u8np, false).gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(marker_event<>{42, 0}));
        REQUIRE(out.check(marker_event<>{42, 2}));
    }

    SECTION("marker, mtov") {
        in.handle(
            bh_spc_event::make_marker(42_u16np, 5_u8np, true).gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{4096 + 42}));
        REQUIRE(out.check(marker_event<>{4096 + 42, 0}));
        REQUIRE(out.check(marker_event<>{4096 + 42, 2}));
    }

    SECTION("multi-mtov") {
        in.handle(
            bh_spc_event::make_multiple_macrotime_overflow(3_u32np).gap_flag(
                gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{i64(4096) * 3}));
        REQUIRE(out.check(time_reached_event<>{i64(4096) * 3}));

        SECTION("photon, no mtov") {
            in.handle(
                bh_spc_event::make_photon(42_u16np, 123_u16np, 5_u8np, false)
                    .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(4096) * 3 + 42}));
            REQUIRE(out.check(time_correlated_detection_event<>{
                i64(4096) * 3 + 42, 5, 123}));
        }

        SECTION("photon, mtov") {
            in.handle(
                bh_spc_event::make_photon(42_u16np, 123_u16np, 5_u8np, true)
                    .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(4096) * 4 + 42}));
            REQUIRE(out.check(time_correlated_detection_event<>{
                i64(4096) * 4 + 42, 5, 123}));
        }

        SECTION("invalid photon") {
            in.handle(bh_spc_event::make_invalid_photon(42_u16np, 123_u16np)
                          .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(4096) * 3 + 42}));
            REQUIRE(out.check(time_reached_event<>{i64(4096) * 3 + 42}));
        }

        SECTION("marker, no mtov") {
            in.handle(bh_spc_event::make_marker(42_u16np, 5_u8np, false)
                          .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(4096) * 3 + 42}));
            REQUIRE(out.check(marker_event<>{i64(4096) * 3 + 42, 0}));
            REQUIRE(out.check(marker_event<>{i64(4096) * 3 + 42, 2}));
        }

        SECTION("marker, mtov") {
            in.handle(bh_spc_event::make_marker(42_u16np, 5_u8np, true)
                          .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(4096) * 4 + 42}));
            REQUIRE(out.check(marker_event<>{i64(4096) * 4 + 42, 0}));
            REQUIRE(out.check(marker_event<>{i64(4096) * 4 + 42, 2}));
        }
    }

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("decode bh spc with fast intensity counter",
          "[decode_bh_spc_with_intensity_counter]") {
    using out_events =
        type_list<time_correlated_detection_event<>, marker_event<>,
                  bulk_counts_event<>, time_reached_event<>, data_lost_event<>,
                  warning_event>;

    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        decode_bh_spc_with_intensity_counter(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    bool const gap = GENERATE(false, true);

    SECTION("no marker 0, no mtov") {
        in.handle(
            bh_spc_event::make_marker(42_u16np, 4_u8np, false).gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(marker_event<>{42, 2}));
    }

    SECTION("no marker 0, mtov") {
        in.handle(
            bh_spc_event::make_marker(42_u16np, 4_u8np, true).gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{4096 + 42}));
        REQUIRE(out.check(marker_event<>{4096 + 42, 2}));
    }

    SECTION("with marker 0, no mtov") {
        in.handle(bh_spc_event::make_marker0_with_intensity_count(
                      42_u16np, 5_u8np, 123_u16np, false)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(bulk_counts_event<>{42, -1, 123}));
        REQUIRE(out.check(marker_event<>{42, 0}));
        REQUIRE(out.check(marker_event<>{42, 2}));
    }

    SECTION("with marker 0, mtov") {
        in.handle(bh_spc_event::make_marker0_with_intensity_count(
                      42_u16np, 5_u8np, 123_u16np, true)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{4096 + 42}));
        REQUIRE(out.check(bulk_counts_event<>{4096 + 42, -1, 123}));
        REQUIRE(out.check(marker_event<>{4096 + 42, 0}));
        REQUIRE(out.check(marker_event<>{4096 + 42, 2}));
    }

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("decode bh spc600 256ch") {
    using out_events =
        type_list<time_correlated_detection_event<>, time_reached_event<>,
                  data_lost_event<>, warning_event>;

    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, decode_bh_spc600_256ch(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    bool const gap = GENERATE(false, true);

    SECTION("photon, no mtov") {
        in.handle(bh_spc600_256ch_event::make_photon(42_u32np, 123_u8np,
                                                     5_u8np, false)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(time_correlated_detection_event<>{42, 5, 123}));
    }

    SECTION("photon, mtov") {
        in.handle(bh_spc600_256ch_event::make_photon(42_u32np, 123_u8np,
                                                     5_u8np, true)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{131'072 + 42}));
        REQUIRE(out.check(
            time_correlated_detection_event<>{131'072 + 42, 5, 123}));
    }

    SECTION("invalid photon") {
        in.handle(
            bh_spc600_256ch_event::make_invalid_photon(42_u32np, 123_u8np)
                .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
    }

    SECTION("multi-mtov") {
        in.handle(
            bh_spc600_256ch_event::make_multiple_macrotime_overflow(3_u32np)
                .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{i64(131'072) * 3}));
        REQUIRE(out.check(time_reached_event<>{i64(131'072) * 3}));

        SECTION("photon, no mtov") {
            in.handle(bh_spc600_256ch_event::make_photon(42_u32np, 123_u8np,
                                                         5_u8np, false)
                          .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(131'072) * 3 + 42}));
            REQUIRE(out.check(time_correlated_detection_event<>{
                i64(131'072) * 3 + 42, 5, 123}));
        }

        SECTION("photon, mtov") {
            in.handle(bh_spc600_256ch_event::make_photon(42_u32np, 123_u8np,
                                                         5_u8np, true)
                          .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(131'072) * 4 + 42}));
            REQUIRE(out.check(time_correlated_detection_event<>{
                i64(131'072) * 4 + 42, 5, 123}));
        }

        SECTION("invalid photon") {
            in.handle(
                bh_spc600_256ch_event::make_invalid_photon(42_u32np, 123_u8np)
                    .gap_flag(gap));
            if (gap)
                REQUIRE(out.check(data_lost_event<>{i64(131'072) * 3 + 42}));
            REQUIRE(out.check(time_reached_event<>{i64(131'072) * 3 + 42}));
        }
    }

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("decode bh spc600 4096ch") {
    using out_events =
        type_list<time_correlated_detection_event<>, time_reached_event<>,
                  data_lost_event<>, warning_event>;

    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, decode_bh_spc600_4096ch(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    bool const gap = GENERATE(false, true);

    SECTION("photon, no mtov") {
        in.handle(bh_spc600_4096ch_event::make_photon(42_u32np, 123_u16np,
                                                      5_u8np, false)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(time_correlated_detection_event<>{42, 5, 123}));
    }

    SECTION("photon, mtov") {
        in.handle(bh_spc600_4096ch_event::make_photon(42_u32np, 123_u16np,
                                                      5_u8np, true)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{16'777'216 + 42}));
        REQUIRE(out.check(
            time_correlated_detection_event<>{16'777'216 + 42, 5, 123}));
    }

    SECTION("invalid photon, no mtov") {
        in.handle(bh_spc600_4096ch_event::make_invalid_photon(42_u32np,
                                                              123_u16np, false)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
    }

    SECTION("invalid photon, mtov") {
        in.handle(bh_spc600_4096ch_event::make_invalid_photon(42_u32np,
                                                              123_u16np, true)
                      .gap_flag(gap));
        if (gap)
            REQUIRE(out.check(data_lost_event<>{16'777'216 + 42}));
        REQUIRE(out.check(time_reached_event<>{16'777'216 + 42}));
    }

    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
