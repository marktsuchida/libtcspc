/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/common.hpp"

#include <cstdint>

#include <catch2/catch_all.hpp>

using namespace flimevt::internal;

static_assert(!false_for_type<int>::value);

TEST_CASE("count_trailing_zeros_32", "[common]") {
    REQUIRE(count_trailing_zeros_32(1) == 0);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(1) == 0);

    REQUIRE(count_trailing_zeros_32(2) == 1);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(2) == 1);

    REQUIRE(count_trailing_zeros_32(3) == 0);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(3) == 0);

    REQUIRE(count_trailing_zeros_32(4) == 2);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(4) == 2);

    REQUIRE(count_trailing_zeros_32(6) == 1);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(6) == 1);

    REQUIRE(count_trailing_zeros_32(1u << 31) == 31);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(1u << 31) == 31);
}

TEST_CASE("as signed or unsigned", "[common]") {
    static_assert(as_signed(std::uint16_t(65535)) == std::int16_t(-1));
    static_assert(as_unsigned(std::int16_t(-1)) == std::uint16_t(65535));
}

TEST_CASE("narrow integer", "[common]") {
    CHECK(narrow<std::uint8_t>(std::uint64_t(100)) == std::uint8_t(100));
}
