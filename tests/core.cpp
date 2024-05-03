/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/core.hpp"

#include "libtcspc/context.hpp"
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

TEST_CASE("introspect: core") {
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

} // namespace tcspc
