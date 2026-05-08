/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/npint_ops.hpp"

#include "libtcspc/npint.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

namespace tcspc::internal {

TEST_CASE("for_each_set_bit") {
    std::vector<int> bits;
    auto record = [&](int b) { bits.push_back(b); };

    for_each_set_bit(0_u32np, record);
    CHECK(bits.empty());

    bits.clear();
    for_each_set_bit(1_u32np, record);
    CHECK(bits == std::vector{0});

    bits.clear();
    for_each_set_bit(2_u32np, record);
    CHECK(bits == std::vector{1});

    bits.clear();
    for_each_set_bit(0b1010_u32np, record);
    CHECK(bits == std::vector{1, 3});

    bits.clear();
    for_each_set_bit(1_u32np << 31, record);
    CHECK(bits == std::vector{31});

    bits.clear();
    for_each_set_bit(~0_u32np, record);
    REQUIRE(bits.size() == 32);
    for (int i = 0; i < 32; ++i)
        CHECK(bits[static_cast<std::size_t>(i)] == i);
}

} // namespace tcspc::internal
