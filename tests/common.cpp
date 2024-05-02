/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/common.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/npint.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

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

} // namespace internal

} // namespace tcspc
