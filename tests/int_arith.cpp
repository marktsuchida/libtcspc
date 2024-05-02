/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/int_arith.hpp"

#include "libtcspc/int_types.hpp"

#include <catch2/catch_test_macros.hpp>

namespace tcspc::internal {

TEST_CASE("as signed or unsigned") {
    STATIC_CHECK(as_signed(u16(65535)) == i16(-1));
    STATIC_CHECK(as_unsigned(i16(-1)) == u16(65535));
}

TEST_CASE("add_sat") {
    CHECK(add_sat(0, 0) == 0);
    CHECK(add_sat(1, 2) == 3);
    CHECK(add_sat(1, -2) == -1);
    CHECK(add_sat(i8(100), i8(27)) == i8(127));
    CHECK(add_sat(i8(100), i8(28)) == i8(127));
    CHECK(add_sat(i8(-100), i8(-28)) == i8(-128));
    CHECK(add_sat(i8(-100), i8(-29)) == i8(-128));
    CHECK(add_sat(u8(100), u8(155)) == u8(255));
    CHECK(add_sat(u8(100), u8(156)) == u8(255));
}

} // namespace tcspc::internal
