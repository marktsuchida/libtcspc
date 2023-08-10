/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/picoquant_t2.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

#include <array>
#include <type_traits>

namespace tcspc {

static_assert(std::is_pod_v<pqt2_picoharp_event>);
static_assert(std::is_pod_v<pqt2_hydraharpv1_event>);
static_assert(std::is_pod_v<pqt2_hydraharpv2_event>);

static_assert(sizeof(pqt2_picoharp_event) == 4);
static_assert(sizeof(pqt2_hydraharpv1_event) == 4);
static_assert(sizeof(pqt2_hydraharpv2_event) == 4);

TEMPLATE_TEST_CASE("pqt2 equality and inequality", "[pqt2_event]",
                   pqt2_picoharp_event, pqt2_hydraharpv1_event,
                   pqt2_hydraharpv2_event) {
    auto const ptrn = std::array<u8, 4>{1, 2, 3, 4};
    CHECK(le_event<TestType>(ptrn) == le_event<TestType>(ptrn));

    auto const zero = std::array<u8, 4>{0, 0, 0, 0};
    auto const nonzero1 = std::array<u8, 4>{0, 0, 0, 1};
    auto const nonzero2 = std::array<u8, 4>{128, 0, 0, 0};
    CHECK(le_event<TestType>(nonzero1) != le_event<TestType>(zero));
    CHECK(le_event<TestType>(nonzero2) != le_event<TestType>(zero));
}

TEST_CASE("pqt2 picoharp event type", "[pqt2_event]") {
    auto const zero = le_event<pqt2_picoharp_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(zero.is_special());
    CHECK_FALSE(zero.is_timetag_overflow());
    CHECK_FALSE(zero.is_sync_event());
    CHECK_FALSE(zero.is_external_marker());

    auto const everything_else = le_event<pqt2_picoharp_event>(
        {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK_FALSE(everything_else.is_special());
    CHECK_FALSE(everything_else.is_timetag_overflow());
    CHECK_FALSE(everything_else.is_sync_event());
    CHECK_FALSE(everything_else.is_external_marker());

    auto const chan1 = le_event<pqt2_picoharp_event>(
        {0b0001'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan1.is_special());
    CHECK_FALSE(chan1.is_timetag_overflow());
    CHECK_FALSE(chan1.is_sync_event());
    CHECK_FALSE(chan1.is_external_marker());

    auto const chan14 = le_event<pqt2_picoharp_event>(
        {0b1110'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan14.is_special());
    CHECK_FALSE(chan14.is_timetag_overflow());
    CHECK_FALSE(chan14.is_sync_event());
    CHECK_FALSE(chan14.is_external_marker());

    auto const overflow = le_event<pqt2_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(overflow.is_special());
    CHECK(overflow.is_timetag_overflow());
    CHECK_FALSE(overflow.is_sync_event());
    CHECK_FALSE(overflow.is_external_marker());

    auto const overflow_everything_else = le_event<pqt2_picoharp_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'0000});
    CHECK(overflow_everything_else.is_special());
    CHECK(overflow_everything_else.is_timetag_overflow());
    CHECK_FALSE(overflow_everything_else.is_sync_event());
    CHECK_FALSE(overflow_everything_else.is_external_marker());

    auto const marker0 = le_event<pqt2_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001});
    CHECK(marker0.is_special());
    CHECK_FALSE(marker0.is_timetag_overflow());
    CHECK_FALSE(marker0.is_sync_event());
    CHECK(marker0.is_external_marker());

    auto const all_markers = le_event<pqt2_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'1111});
    CHECK(all_markers.is_special());
    CHECK_FALSE(all_markers.is_timetag_overflow());
    CHECK_FALSE(all_markers.is_sync_event());
    CHECK(all_markers.is_external_marker());
}

TEMPLATE_TEST_CASE("pqt2 hydraharp event type", "[pqt2_event]",
                   pqt2_hydraharpv1_event, pqt2_hydraharpv2_event) {
    auto const zero = le_event<TestType>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(zero.is_special());
    CHECK_FALSE(zero.is_timetag_overflow());
    CHECK_FALSE(zero.is_sync_event());
    CHECK_FALSE(zero.is_external_marker());

    auto const everything_else = le_event<TestType>(
        {0b0111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK_FALSE(everything_else.is_special());
    CHECK_FALSE(everything_else.is_timetag_overflow());
    CHECK_FALSE(everything_else.is_sync_event());
    CHECK_FALSE(everything_else.is_external_marker());

    auto const chan1 = le_event<TestType>(
        {0b0000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan1.is_special());
    CHECK_FALSE(chan1.is_timetag_overflow());
    CHECK_FALSE(chan1.is_sync_event());
    CHECK_FALSE(chan1.is_external_marker());

    auto const chan63 = le_event<TestType>(
        {0b0111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan63.is_special());
    CHECK_FALSE(chan63.is_timetag_overflow());
    CHECK_FALSE(chan63.is_sync_event());
    CHECK_FALSE(chan63.is_external_marker());

    auto const overflow = le_event<TestType>(
        {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(overflow.is_special());
    CHECK(overflow.is_timetag_overflow());
    CHECK_FALSE(overflow.is_sync_event());
    CHECK_FALSE(overflow.is_external_marker());

    auto const overflow_everything_else = le_event<TestType>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK(overflow_everything_else.is_special());
    CHECK(overflow_everything_else.is_timetag_overflow());
    CHECK_FALSE(overflow_everything_else.is_sync_event());
    CHECK_FALSE(overflow_everything_else.is_external_marker());

    auto const marker0 = le_event<TestType>(
        {0b1000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0001});
    CHECK(marker0.is_special());
    CHECK_FALSE(marker0.is_timetag_overflow());
    CHECK_FALSE(marker0.is_sync_event());
    CHECK(marker0.is_external_marker());

    auto const all_markers = le_event<TestType>(
        {0b1001'1110, 0b0000'0000, 0b0000'0000, 0b0000'1111});
    CHECK(all_markers.is_special());
    CHECK_FALSE(all_markers.is_timetag_overflow());
    CHECK_FALSE(all_markers.is_sync_event());
    CHECK(all_markers.is_external_marker());

    auto const out_of_range_markers = le_event<TestType>(
        {0b1010'0000, 0b0000'0000, 0b0000'0000, 0b0000'1111});
    CHECK(out_of_range_markers.is_special());
    CHECK_FALSE(out_of_range_markers.is_timetag_overflow());
    CHECK_FALSE(out_of_range_markers.is_sync_event());
    CHECK_FALSE(out_of_range_markers.is_external_marker());
}

TEST_CASE("pqt2 picoharp read channel", "[pqt2_event]") {
    auto const chan0 = le_event<pqt2_picoharp_event>(
        {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(chan0.is_special());
    CHECK(chan0.channel() == 0_u8np);

    auto const chan14 = le_event<pqt2_picoharp_event>(
        {0b1110'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(chan14.is_special());
    CHECK(chan14.channel() == 14_u8np);
}

TEMPLATE_TEST_CASE("pqt2 hydraharp read channel", "[pqt2_event]",
                   pqt2_hydraharpv1_event, pqt2_hydraharpv2_event) {
    auto const chan0 = le_event<TestType>(
        {0b0000'0001, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(chan0.is_special());
    CHECK(chan0.channel() == 0_u8np);

    auto const chan63 = le_event<TestType>(
        {0b0111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(chan63.is_special());
    CHECK(chan63.channel() == 63_u8np);
}

TEST_CASE("pqt2 picoharp read time tag", "[pqt2_event]") {
    auto const timetag0 = le_event<pqt2_picoharp_event>(
        {0b0001'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(timetag0.is_special());
    CHECK(timetag0.timetag() == 0_u32np);

    auto const timetag_max = le_event<pqt2_picoharp_event>(
        {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(timetag_max.is_special());
    CHECK(timetag_max.timetag() == 268'435'455_u32np);
}

TEMPLATE_TEST_CASE("pqt2 hydraharp read time tag", "[pqt2_event]",
                   pqt2_hydraharpv1_event, pqt2_hydraharpv2_event) {
    auto const timetag0 = le_event<TestType>(
        {0b0111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(timetag0.is_special());
    CHECK(timetag0.timetag() == 0_u32np);

    auto const timetag_max = le_event<TestType>(
        {0b0000'0001, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(timetag_max.is_special());
    CHECK(timetag_max.timetag() == 33'554'431_u32np);
}

TEST_CASE("pqt2 picoharp read marker time tag", "[pqt2_event]") {
    // Marker time tag should have low 4 bits zeroed.
    auto const marker_timetag = le_event<pqt2_picoharp_event>(
        {0b1111'0011, 0b1100'0011, 0b0011'1100, 0b1111'1010});
    REQUIRE(marker_timetag.is_external_marker());
    CHECK(marker_timetag.external_marker_timetag() ==
          0b0011'1100'0011'0011'1100'1111'0000_u32np);
}

TEMPLATE_TEST_CASE("pqt2 hydraharp read marker time tag", "[pqt2_event]",
                   pqt2_hydraharpv1_event, pqt2_hydraharpv2_event) {
    auto const timetag0 = le_event<TestType>(
        {0b1001'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(timetag0.is_external_marker());
    CHECK(timetag0.timetag() == 0_u32np);

    auto const timetag_max = le_event<TestType>(
        {0b1001'0001, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE(timetag_max.is_external_marker());
    CHECK(timetag_max.timetag() == 33'554'431_u32np);
}

TEST_CASE("pqt2 picoharp read timetag overflow count", "[pqt2_event]") {
    auto const zeros = le_event<pqt2_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(zeros.is_timetag_overflow());
    CHECK(zeros.timetag_overflow_count() == 1_u32np);

    auto const ones = le_event<pqt2_picoharp_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'0000});
    REQUIRE(ones.is_timetag_overflow());
    CHECK(ones.timetag_overflow_count() == 1_u32np);
}

TEST_CASE("pqt2 hydraharpv1 read time tag overflow count", "[pqt2_event]") {
    auto const zeros = le_event<pqt2_hydraharpv1_event>(
        {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(zeros.is_timetag_overflow());
    CHECK(zeros.timetag_overflow_count() == 1_u32np);

    auto const ones = le_event<pqt2_hydraharpv1_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE(ones.is_timetag_overflow());
    CHECK(ones.timetag_overflow_count() == 1_u32np);
}

TEST_CASE("pqt2 hydraharpv2 read time tag overflow count", "[pqt2_event]") {
    auto const zeros = le_event<pqt2_hydraharpv2_event>(
        {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(zeros.is_timetag_overflow());
    CHECK(zeros.timetag_overflow_count() == 0_u32np);

    auto const ones = le_event<pqt2_hydraharpv2_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE(ones.is_timetag_overflow());
    CHECK(ones.timetag_overflow_count() == 33'554'431_u32np);
}

TEST_CASE("pqt2 picoharp read external marker bits", "[pqt2_event]") {
    auto const marker1 = le_event<pqt2_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001});
    REQUIRE(marker1.is_external_marker());
    CHECK(marker1.external_marker_bits() == 1_u8np);

    auto const marker_all = le_event<pqt2_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'1111});
    REQUIRE(marker_all.is_external_marker());
    CHECK(marker_all.external_marker_bits() == 15_u8np);
}

TEMPLATE_TEST_CASE("pqt2 hydraharp read external marker bits", "[pqt2_event]",
                   pqt2_hydraharpv1_event, pqt2_hydraharpv2_event) {
    auto const marker1 = le_event<TestType>(
        {0b1000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(marker1.is_external_marker());
    CHECK(marker1.external_marker_bits() == 1_u8np);

    auto const marker_all = le_event<TestType>(
        {0b1001'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(marker_all.is_external_marker());
    CHECK(marker_all.external_marker_bits() == 15_u8np);
}

TEST_CASE("pqt2 picoharp assign", "[pqt2_event]") {
    CHECK(pqt2_picoharp_event::make_nonspecial(0_u32np, 0_u8np) ==
          le_event<pqt2_picoharp_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(pqt2_picoharp_event::make_nonspecial(1_u32np, 2_u8np) ==
          le_event<pqt2_picoharp_event>(
              {0b0010'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(pqt2_picoharp_event::make_nonspecial(268'435'454_u32np, 14_u8np) ==
          le_event<pqt2_picoharp_event>(
              {0b1110'1111, 0b1111'1111, 0b1111'1111, 0b1111'1110}));
    CHECK(pqt2_picoharp_event::make_nonspecial(268'435'455_u32np, 14_u8np) ==
          le_event<pqt2_picoharp_event>(
              {0b1110'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(pqt2_picoharp_event::make_timetag_overflow() ==
          le_event<pqt2_picoharp_event>(
              {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));

    CHECK(pqt2_picoharp_event::make_external_marker(0_u32np, 1_u8np) ==
          le_event<pqt2_picoharp_event>(
              {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(
        pqt2_picoharp_event::make_external_marker(268'435'455_u32np, 3_u8np) ==
        le_event<pqt2_picoharp_event>(
            {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'0011}));
    CHECK(pqt2_picoharp_event::make_external_marker(268'435'455_u32np,
                                                    15_u8np) ==
          le_event<pqt2_picoharp_event>(
              {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));
}

TEMPLATE_TEST_CASE("pqt2 hydraharp assign", "[pqt2_event]",
                   pqt2_hydraharpv1_event, pqt2_hydraharpv2_event) {
    CHECK(TestType::make_nonspecial(0_u32np, 0_u8np) ==
          le_event<TestType>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(TestType::make_nonspecial(1_u32np, 2_u8np) ==
          le_event<TestType>(
              {0b0000'0100, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(TestType::make_nonspecial(33'554'430_u32np, 62_u8np) ==
          le_event<TestType>(
              {0b0111'1101, 0b1111'1111, 0b1111'1111, 0b1111'1110}));
    CHECK(TestType::make_nonspecial(33'554'431_u32np, 63_u8np) ==
          le_event<TestType>(
              {0b0111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(pqt2_hydraharpv1_event::make_timetag_overflow() ==
          le_event<pqt2_hydraharpv1_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0001}));

    CHECK(pqt2_hydraharpv2_event::make_timetag_overflow() ==
          pqt2_hydraharpv2_event::make_timetag_overflow(1_u32np));
    CHECK(pqt2_hydraharpv2_event::make_timetag_overflow(0_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(pqt2_hydraharpv2_event::make_timetag_overflow(1_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(pqt2_hydraharpv2_event::make_timetag_overflow(33'554'430_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1110}));
    CHECK(pqt2_hydraharpv2_event::make_timetag_overflow(33'554'431_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(pqt2_hydraharpv2_event::make_sync(0_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(pqt2_hydraharpv2_event::make_sync(1_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(pqt2_hydraharpv2_event::make_sync(33'554'430_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1000'0001, 0b1111'1111, 0b1111'1111, 0b1111'1110}));
    CHECK(pqt2_hydraharpv2_event::make_sync(33'554'431_u32np) ==
          le_event<pqt2_hydraharpv2_event>(
              {0b1000'0001, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(TestType::make_external_marker(0_u32np, 1_u8np) ==
          le_event<TestType>(
              {0b1000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(TestType::make_external_marker(33'554'430_u32np, 14_u8np) ==
          le_event<TestType>(
              {0b1001'1101, 0b1111'1111, 0b1111'1111, 0b1111'1110}));
    CHECK(TestType::make_external_marker(33'554'431_u32np, 15_u8np) ==
          le_event<TestType>(
              {0b1001'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));
}

} // namespace tcspc
