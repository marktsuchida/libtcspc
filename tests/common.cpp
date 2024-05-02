/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/common.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/npint.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace tcspc {

TEST_CASE("type constraints: null_sink") {
    struct e0 {};
    STATIC_CHECK(is_processor_v<null_sink, e0>);
}

TEST_CASE("type constraints: null_source") {
    STATIC_CHECK(is_processor_v<decltype(null_source(sink_events<>()))>);
}

TEST_CASE("introspect: common") {
    check_introspect_simple_sink(null_sink());
    check_introspect_simple_processor(null_source(null_sink()));
}

TEST_CASE("null sink") {
    auto sink = null_sink();
    sink.handle(123);
    sink.handle(std::string("hello"));
}

TEST_CASE("null source") {
    auto ctx = context::create();
    auto src = null_source(capture_output<type_list<>>(
        ctx->tracker<capture_output_access>("out")));
    auto out = ctx->access<capture_output_access>("out");
    src.flush();
    REQUIRE(out.check_flushed());
}

namespace internal {

static_assert(!false_for_type<int>::value);

TEST_CASE("add_sat") {
    REQUIRE(add_sat(0, 0) == 0);
    REQUIRE(add_sat(1, 2) == 3);
    REQUIRE(add_sat(1, -2) == -1);
    REQUIRE(add_sat(i8(100), i8(27)) == i8(127));
    REQUIRE(add_sat(i8(100), i8(28)) == i8(127));
    REQUIRE(add_sat(i8(-100), i8(-28)) == i8(-128));
    REQUIRE(add_sat(i8(-100), i8(-29)) == i8(-128));
    REQUIRE(add_sat(u8(100), u8(155)) == u8(255));
    REQUIRE(add_sat(u8(100), u8(156)) == u8(255));
}

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

TEST_CASE("as signed or unsigned") {
    static_assert(as_signed(std::uint16_t(65535)) == std::int16_t(-1));
    static_assert(as_unsigned(std::int16_t(-1)) == std::uint16_t(65535));
}

} // namespace internal

} // namespace tcspc
