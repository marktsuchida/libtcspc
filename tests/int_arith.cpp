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
    STATIC_CHECK(as_signed(u8(255)) == i8(-1));
    STATIC_CHECK(as_unsigned(i8(-1)) == u8(255));
    STATIC_CHECK(as_signed(u16(65535)) == i16(-1));
    STATIC_CHECK(as_unsigned(i16(-1)) == u16(65535));
}

TEST_CASE("cmp_less") {
    STATIC_CHECK_FALSE(cmp_less(0, 0));
    STATIC_CHECK(cmp_less(-1, 0));
    STATIC_CHECK(cmp_less(0, 1));
    STATIC_CHECK(cmp_less(0u, 1u));

    STATIC_CHECK(cmp_less(i8(-1), u8(0)));
    STATIC_CHECK(cmp_less(i32(-1), u32(0)));

    STATIC_CHECK(cmp_less(u8(0), i8(1)));
    STATIC_CHECK(cmp_less(u32(0), i32(1)));
}

TEST_CASE("is_type_in_range") {
    STATIC_CHECK(is_type_in_range<i8>(i8{0}));
    STATIC_CHECK(is_type_in_range<u8>(u8{0}));
    STATIC_CHECK_FALSE(is_type_in_range<i8>(u8{0}));
    STATIC_CHECK_FALSE(is_type_in_range<u8>(i8{0}));
    STATIC_CHECK_FALSE(is_type_in_range<i8>(i16{0}));
    STATIC_CHECK_FALSE(is_type_in_range<u8>(u16{0}));
    STATIC_CHECK_FALSE(is_type_in_range<i8>(u16{0}));
    STATIC_CHECK_FALSE(is_type_in_range<u8>(i16{0}));
    STATIC_CHECK(is_type_in_range<i16>(i8{0}));
    STATIC_CHECK(is_type_in_range<u16>(u8{0}));
    STATIC_CHECK(is_type_in_range<i16>(u8{0}));
    STATIC_CHECK_FALSE(is_type_in_range<u16>(i8{0}));
}

TEST_CASE("in_range") {
    STATIC_CHECK(in_range<i8>(127));
    STATIC_CHECK(in_range<i8>(-128));
    STATIC_CHECK_FALSE(in_range<i8>(128));
    STATIC_CHECK_FALSE(in_range<i8>(-129));
    STATIC_CHECK(in_range<u8>(255));
    STATIC_CHECK(in_range<u8>(0));
    STATIC_CHECK_FALSE(in_range<u8>(256));
    STATIC_CHECK_FALSE(in_range<u8>(-1));
}

TEST_CASE("convert_with_check") {
    STATIC_CHECK(convert_with_check<i8>(127) == i8(127));
    STATIC_CHECK(convert_with_check<i8>(-128) == i8(-128));
    CHECK_THROWS_AS(convert_with_check<i8>(128), arithmetic_overflow_error);
    CHECK_THROWS_AS(convert_with_check<i8>(-129), arithmetic_overflow_error);
    STATIC_CHECK(convert_with_check<u8>(255) == u8(255));
    STATIC_CHECK(convert_with_check<u8>(0) == u8(0));
    CHECK_THROWS_AS(convert_with_check<u8>(256), arithmetic_overflow_error);
    CHECK_THROWS_AS(convert_with_check<u8>(-1), arithmetic_overflow_error);
}

TEST_CASE("add_with_check") {
    CHECK(add_with_check(u8(254), u8(1)) == u8(255));
    CHECK(add_with_check(i8(126), i8(1)) == i8(127));
    CHECK_THROWS_AS(add_with_check(u8(254), u8(2)), arithmetic_overflow_error);
    CHECK_THROWS_AS(add_with_check(i8(126), i8(2)), arithmetic_overflow_error);
    CHECK(add_with_check(i8(-127), i8(-1)) == i8(-128));
    CHECK_THROWS_AS(add_with_check(i8(-127), i8(-2)),
                    arithmetic_overflow_error);
}

TEST_CASE("subtract_with_check") {
    CHECK(subtract_with_check(u8(1), u8(1)) == u8(0));
    CHECK(subtract_with_check(i8(-127), i8(1)) == i8(-128));
    CHECK_THROWS_AS(subtract_with_check(u8(1), u8(2)),
                    arithmetic_overflow_error);
    CHECK_THROWS_AS(subtract_with_check(i8(-127), i8(2)),
                    arithmetic_overflow_error);
    CHECK(subtract_with_check(i8(126), i8(-1)) == i8(127));
    CHECK_THROWS_AS(subtract_with_check(i8(126), i8(-2)),
                    arithmetic_overflow_error);
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

TEST_CASE("add_with_wrap") {
    STATIC_CHECK(add_with_wrap(i8(127), i8(1)) == i8(-128));
}

TEST_CASE("subtract_with_wrap") {
    STATIC_CHECK(subtract_with_wrap(i8(-128), i8(1)) == i8(127));
}

} // namespace tcspc::internal
