/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/common.hpp"

#include <cstdint>

#include <catch2/catch_all.hpp>

namespace tcspc::internal {

static_assert(!false_for_type<int>::value);

TEST_CASE("count_trailing_zeros_32", "[common]") {
    REQUIRE(count_trailing_zeros_32(1_u32np) == 0);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(1_u32np) == 0);

    REQUIRE(count_trailing_zeros_32(2_u32np) == 1);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(2_u32np) == 1);

    REQUIRE(count_trailing_zeros_32(3_u32np) == 0);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(3_u32np) == 0);

    REQUIRE(count_trailing_zeros_32(4_u32np) == 2);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(4_u32np) == 2);

    REQUIRE(count_trailing_zeros_32(6_u32np) == 1);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(6_u32np) == 1);

    REQUIRE(count_trailing_zeros_32(1_u32np << 31) == 31);
    REQUIRE(count_trailing_zeros_32_nonintrinsic(1_u32np << 31) == 31);
}

TEST_CASE("as signed or unsigned", "[common]") {
    static_assert(as_signed(std::uint16_t(65535)) == std::int16_t(-1));
    static_assert(as_unsigned(std::int16_t(-1)) == std::uint16_t(65535));
}

TEST_CASE("narrow integer", "[common]") {
    CHECK(narrow<std::uint8_t>(std::uint64_t(100)) == std::uint8_t(100));
}

} // namespace tcspc::internal
