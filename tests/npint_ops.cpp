/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/npint_ops.hpp"

#include "libtcspc/npint.hpp"

#include <catch2/catch_test_macros.hpp>

namespace tcspc::internal {

TEST_CASE("count_trailing_zeros_32") {
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

} // namespace tcspc::internal
