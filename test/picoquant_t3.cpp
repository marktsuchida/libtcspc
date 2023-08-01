/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/picoquant_t3.hpp"

#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

#include <array>
#include <type_traits>

namespace tcspc {

static_assert(std::is_pod_v<pqt3_picoharp_event>);
static_assert(std::is_pod_v<pqt3_hydraharpv1_event>);
static_assert(std::is_pod_v<pqt3_hydraharpv2_event>);

static_assert(sizeof(pqt3_picoharp_event) == 4);
static_assert(sizeof(pqt3_hydraharpv1_event) == 4);
static_assert(sizeof(pqt3_hydraharpv2_event) == 4);

TEMPLATE_TEST_CASE("pqt3 equality and inequality", "[pqt3_event]",
                   pqt3_picoharp_event, pqt3_hydraharpv1_event,
                   pqt3_hydraharpv2_event) {
    auto const ptrn = std::array<u8, 4>{1, 2, 3, 4};
    CHECK(le_event<TestType>(ptrn) == le_event<TestType>(ptrn));

    auto const zero = std::array<u8, 4>{0, 0, 0, 0};
    auto const nonzero1 = std::array<u8, 4>{0, 0, 0, 1};
    auto const nonzero2 = std::array<u8, 4>{128, 0, 0, 0};
    CHECK(le_event<TestType>(nonzero1) != le_event<TestType>(zero));
    CHECK(le_event<TestType>(nonzero2) != le_event<TestType>(zero));
}

TEST_CASE("pqt3 picoharp event type", "[pqt3_event]") {
    auto const zero = le_event<pqt3_picoharp_event>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(zero.is_special());
    CHECK_FALSE(zero.is_nsync_overflow());
    CHECK_FALSE(zero.is_external_marker());

    auto const everything_else = le_event<pqt3_picoharp_event>(
        {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK_FALSE(everything_else.is_special());
    CHECK_FALSE(everything_else.is_nsync_overflow());
    CHECK_FALSE(everything_else.is_external_marker());

    auto const chan1 = le_event<pqt3_picoharp_event>(
        {0b0001'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan1.is_special());
    CHECK_FALSE(chan1.is_nsync_overflow());
    CHECK_FALSE(chan1.is_external_marker());

    auto const chan14 = le_event<pqt3_picoharp_event>(
        {0b1110'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan14.is_special());
    CHECK_FALSE(chan14.is_nsync_overflow());
    CHECK_FALSE(chan14.is_external_marker());

    auto const overflow = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(overflow.is_special());
    CHECK(overflow.is_nsync_overflow());
    CHECK_FALSE(overflow.is_external_marker());

    auto const overflow_everything_else = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b1111'1111, 0b1111'1111});
    CHECK(overflow_everything_else.is_special());
    CHECK(overflow_everything_else.is_nsync_overflow());
    CHECK_FALSE(overflow_everything_else.is_external_marker());

    auto const marker0 = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000});
    CHECK(marker0.is_special());
    CHECK_FALSE(marker0.is_nsync_overflow());
    CHECK(marker0.is_external_marker());

    auto const all_markers = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'1111, 0b0000'0000, 0b0000'0000});
    CHECK(all_markers.is_special());
    CHECK_FALSE(all_markers.is_nsync_overflow());
    CHECK(all_markers.is_external_marker());

    auto const out_of_range_marker = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0001'0000, 0b0000'0000, 0b0000'0000});
    CHECK(out_of_range_marker.is_special());
    CHECK_FALSE(out_of_range_marker.is_nsync_overflow());
    CHECK_FALSE(out_of_range_marker.is_external_marker());
}

TEMPLATE_TEST_CASE("pqt3 hydraharp event type", "[pqt3_event]",
                   pqt3_hydraharpv1_event, pqt3_hydraharpv2_event) {
    auto const zero = le_event<TestType>(
        {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(zero.is_special());
    CHECK_FALSE(zero.is_nsync_overflow());
    CHECK_FALSE(zero.is_external_marker());

    auto const everything_else = le_event<TestType>(
        {0b0111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK_FALSE(everything_else.is_special());
    CHECK_FALSE(everything_else.is_nsync_overflow());
    CHECK_FALSE(everything_else.is_external_marker());

    auto const chan1 = le_event<TestType>(
        {0b0000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan1.is_special());
    CHECK_FALSE(chan1.is_nsync_overflow());
    CHECK_FALSE(chan1.is_external_marker());

    auto const chan63 = le_event<TestType>(
        {0b0111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK_FALSE(chan63.is_special());
    CHECK_FALSE(chan63.is_nsync_overflow());
    CHECK_FALSE(chan63.is_external_marker());

    auto const overflow = le_event<TestType>(
        {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(overflow.is_special());
    CHECK(overflow.is_nsync_overflow());
    CHECK_FALSE(overflow.is_external_marker());

    auto const overflow_everything_else = le_event<TestType>(
        {0b1111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    CHECK(overflow_everything_else.is_special());
    CHECK(overflow_everything_else.is_nsync_overflow());
    CHECK_FALSE(overflow_everything_else.is_external_marker());

    auto const marker0 = le_event<TestType>(
        {0b1000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(marker0.is_special());
    CHECK_FALSE(marker0.is_nsync_overflow());
    CHECK(marker0.is_external_marker());

    auto const all_markers = le_event<TestType>(
        {0b1001'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(all_markers.is_special());
    CHECK_FALSE(all_markers.is_nsync_overflow());
    CHECK(all_markers.is_external_marker());

    auto const out_of_range_marker = le_event<TestType>(
        {0b1010'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    CHECK(out_of_range_marker.is_special());
    CHECK_FALSE(out_of_range_marker.is_nsync_overflow());
    CHECK_FALSE(out_of_range_marker.is_external_marker());
}

TEST_CASE("pqt3 picoharp read channel", "[pqt3_event]") {
    auto const chan0 = le_event<pqt3_picoharp_event>(
        {0b0000'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(chan0.is_special());
    CHECK(chan0.channel() == 0_u8np);

    auto const chan14 = le_event<pqt3_picoharp_event>(
        {0b1110'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(chan14.is_special());
    CHECK(chan14.channel() == 14_u8np);
}

TEMPLATE_TEST_CASE("pqt3 hydraharp read channel", "[pqt3_event]",
                   pqt3_hydraharpv1_event, pqt3_hydraharpv2_event) {
    auto const chan0 = le_event<TestType>(
        {0b0000'0001, 0b1111'1111, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(chan0.is_special());
    CHECK(chan0.channel() == 0_u8np);

    auto const chan63 = le_event<TestType>(
        {0b0111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(chan63.is_special());
    CHECK(chan63.channel() == 63_u8np);
}

TEST_CASE("pqt3 picoharp read dtime", "[pqt3_event]") {
    auto const dtime0 = le_event<pqt3_picoharp_event>(
        {0b0001'0000, 0b0000'0000, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(dtime0.is_special());
    CHECK(dtime0.dtime() == 0_u16np);

    auto const dtime_max = le_event<pqt3_picoharp_event>(
        {0b0000'1111, 0b1111'1111, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(dtime_max.is_special());
    CHECK(dtime_max.dtime() == 4095_u16np);
}

TEMPLATE_TEST_CASE("pqt3 hydraharp read dtime", "[pqt3_event]",
                   pqt3_hydraharpv1_event, pqt3_hydraharpv2_event) {
    auto const dtime0 = le_event<TestType>(
        {0b0000'0010, 0b0000'0000, 0b0000'0011, 0b1111'1111});
    REQUIRE_FALSE(dtime0.is_special());
    CHECK(dtime0.dtime() == 0_u16np);

    auto const dtime_max = le_event<TestType>(
        {0b0000'0001, 0b1111'1111, 0b1111'1100, 0b0000'0000});
    REQUIRE_FALSE(dtime_max.is_special());
    CHECK(dtime_max.dtime() == 32767_u16np);
}

TEST_CASE("pqt3 picoharp read nsync", "[pqt3_event]") {
    auto const nsync0 = le_event<pqt3_picoharp_event>(
        {0b1110'1111, 0b1111'1111, 0b0000'0000, 0b0000'0000});
    REQUIRE_FALSE(nsync0.is_special());
    CHECK(nsync0.nsync() == 0_u16np);

    auto const nsync_max = le_event<pqt3_picoharp_event>(
        {0b0000'0000, 0b0000'0000, 0b1111'1111, 0b1111'1111});
    REQUIRE_FALSE(nsync_max.is_special());
    CHECK(nsync_max.nsync() == 65535_u16np);
}

TEMPLATE_TEST_CASE("pqt3 hydraharp read nsync", "[pqt3_event]",
                   pqt3_hydraharpv1_event, pqt3_hydraharpv2_event) {
    auto const nsync0 = le_event<TestType>(
        {0b0000'0001, 0b1111'1111, 0b1111'1100, 0b0000'0000});
    REQUIRE_FALSE(nsync0.is_special());
    CHECK(nsync0.nsync() == 0_u16np);

    auto const nsync_max = le_event<TestType>(
        {0b0000'0010, 0b0000'0000, 0b0000'0011, 0b1111'1111});
    REQUIRE_FALSE(nsync_max.is_special());
    CHECK(nsync_max.nsync() == 1023_u16np);
}

TEST_CASE("pqt3 picoharp read nsync overflow count", "[pqt3_event]") {
    auto const dtime0 = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(dtime0.is_nsync_overflow());
    CHECK(dtime0.nsync_overflow_count() == 1_u16np);

    auto const dtime_max = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'0000, 0b1111'1111, 0b1111'1111});
    REQUIRE(dtime_max.is_nsync_overflow());
    CHECK(dtime_max.nsync_overflow_count() == 1_u16np);
}

TEST_CASE("pqt3 hydraharlv1 read nsync overflow count", "[pqt3_event]") {
    auto const zeros = le_event<pqt3_hydraharpv1_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1100, 0b0000'0000});
    REQUIRE(zeros.is_nsync_overflow());
    CHECK(zeros.nsync_overflow_count() == 1_u16np);

    auto const ones = le_event<pqt3_hydraharpv1_event>(
        {0b1111'1110, 0b0000'0000, 0b0000'0011, 0b1111'1111});
    REQUIRE(ones.is_nsync_overflow());
    CHECK(ones.nsync_overflow_count() == 1_u16np);
}

TEST_CASE("pqt3 hydraharlv2 read nsync overflow count", "[pqt3_event]") {
    auto const zeros = le_event<pqt3_hydraharpv2_event>(
        {0b1111'1111, 0b1111'1111, 0b1111'1100, 0b0000'0000});
    REQUIRE(zeros.is_nsync_overflow());
    CHECK(zeros.nsync_overflow_count() == 0_u16np);

    auto const ones = le_event<pqt3_hydraharpv2_event>(
        {0b1111'1110, 0b0000'0000, 0b0000'0011, 0b1111'1111});
    REQUIRE(ones.is_nsync_overflow());
    CHECK(ones.nsync_overflow_count() == 1023_u16np);
}

TEST_CASE("pqt3 picoharp read external marker bits", "[pqt3_event]") {
    auto const marker1 = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000});
    REQUIRE(marker1.is_external_marker());
    CHECK(marker1.external_marker_bits() == 1_u16np);

    auto const marker_all = le_event<pqt3_picoharp_event>(
        {0b1111'0000, 0b0000'1111, 0b0000'0000, 0b0000'0000});
    REQUIRE(marker_all.is_external_marker());
    CHECK(marker_all.external_marker_bits() == 15_u16np);
}

TEMPLATE_TEST_CASE("pqt3 hydraharp read external marker bits", "[pqt3_event]",
                   pqt3_hydraharpv1_event, pqt3_hydraharpv2_event) {
    auto const marker1 = le_event<TestType>(
        {0b1000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(marker1.is_external_marker());
    CHECK(marker1.external_marker_bits() == 1_u8np);

    auto const marker_all = le_event<TestType>(
        {0b1001'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000});
    REQUIRE(marker_all.is_external_marker());
    CHECK(marker_all.external_marker_bits() == 15_u8np);
}

TEST_CASE("pqt3 picoharp assign", "[pqt3_event]") {
    CHECK(pqt3_picoharp_event{}.assign_nonspecial(0_u16np, 0_u8np, 0_u16np) ==
          le_event<pqt3_picoharp_event>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(pqt3_picoharp_event{}.assign_nonspecial(1_u16np, 2_u8np, 3_u16np) ==
          le_event<pqt3_picoharp_event>(
              {0b0010'0000, 0b0000'0011, 0b0000'0000, 0b0000'0001}));
    CHECK(pqt3_picoharp_event{}.assign_nonspecial(65534_u16np, 14_u8np,
                                                  4094_u16np) ==
          le_event<pqt3_picoharp_event>(
              {0b1110'1111, 0b1111'1110, 0b1111'1111, 0b1111'1110}));
    CHECK(pqt3_picoharp_event{}.assign_nonspecial(65535_u16np, 14_u8np,
                                                  4095_u16np) ==
          le_event<pqt3_picoharp_event>(
              {0b1110'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(pqt3_picoharp_event{}.assign_nsync_overflow() ==
          le_event<pqt3_picoharp_event>(
              {0b1111'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));

    CHECK(pqt3_picoharp_event{}.assign_external_marker(0_u16np, 1_u8np) ==
          le_event<pqt3_picoharp_event>(
              {0b1111'0000, 0b0000'0001, 0b0000'0000, 0b0000'0000}));
    CHECK(pqt3_picoharp_event{}.assign_external_marker(65534_u16np, 14_u8np) ==
          le_event<pqt3_picoharp_event>(
              {0b1111'0000, 0b0000'1110, 0b1111'1111, 0b1111'1110}));
    CHECK(pqt3_picoharp_event{}.assign_external_marker(65535_u16np, 15_u8np) ==
          le_event<pqt3_picoharp_event>(
              {0b1111'0000, 0b0000'1111, 0b1111'1111, 0b1111'1111}));
}

TEMPLATE_TEST_CASE("pqt3 hydraharp assign", "[pqt3_event]",
                   pqt3_hydraharpv1_event, pqt3_hydraharpv2_event) {
    CHECK(TestType{}.assign_nonspecial(0_u16np, 0_u8np, 0_u16np) ==
          le_event<TestType>(
              {0b0000'0000, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(TestType{}.assign_nonspecial(1_u16np, 2_u8np, 3_u16np) ==
          le_event<TestType>(
              {0b0000'0100, 0b0000'0000, 0b0000'1100, 0b0000'0001}));
    CHECK(TestType{}.assign_nonspecial(1022_u16np, 62_u8np, 32766_u16np) ==
          le_event<TestType>(
              {0b0111'1101, 0b1111'1111, 0b1111'1011, 0b1111'1110}));
    CHECK(TestType{}.assign_nonspecial(1023_u16np, 63_u8np, 32767_u16np) ==
          le_event<TestType>(
              {0b0111'1111, 0b1111'1111, 0b1111'1111, 0b1111'1111}));

    CHECK(pqt3_hydraharpv1_event{}.assign_nsync_overflow() ==
          le_event<pqt3_hydraharpv1_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000}));

    CHECK(pqt3_hydraharpv2_event{}.assign_nsync_overflow() ==
          pqt3_hydraharpv2_event{}.assign_nsync_overflow(1_u16np));
    CHECK(pqt3_hydraharpv2_event{}.assign_nsync_overflow(0_u16np) ==
          le_event<pqt3_hydraharpv2_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(pqt3_hydraharpv2_event{}.assign_nsync_overflow(1_u16np) ==
          le_event<pqt3_hydraharpv2_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0000, 0b0000'0001}));
    CHECK(pqt3_hydraharpv2_event{}.assign_nsync_overflow(1022_u16np) ==
          le_event<pqt3_hydraharpv2_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0011, 0b1111'1110}));
    CHECK(pqt3_hydraharpv2_event{}.assign_nsync_overflow(1023_u16np) ==
          le_event<pqt3_hydraharpv2_event>(
              {0b1111'1110, 0b0000'0000, 0b0000'0011, 0b1111'1111}));

    CHECK(TestType{}.assign_external_marker(0_u16np, 1_u8np) ==
          le_event<TestType>(
              {0b1000'0010, 0b0000'0000, 0b0000'0000, 0b0000'0000}));
    CHECK(TestType{}.assign_external_marker(1022_u16np, 14_u8np) ==
          le_event<TestType>(
              {0b1001'1100, 0b0000'0000, 0b0000'0011, 0b1111'1110}));
    CHECK(TestType{}.assign_external_marker(1023_u16np, 15_u8np) ==
          le_event<TestType>(
              {0b1001'1110, 0b0000'0000, 0b0000'0011, 0b1111'1111}));
}

} // namespace tcspc
