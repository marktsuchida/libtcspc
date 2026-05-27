/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/core.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

namespace tcspc {

TEST_CASE("type constraints: sink_all") {
    struct e0 {};
    STATIC_CHECK(processor<decltype(sink_all()), e0>);
}

TEST_CASE("type constraints: source_nothing") {
    STATIC_CHECK(processor<decltype(source_nothing(sink_only<>()))>);
}

TEST_CASE("introspect: core") {
    check_introspect_simple_sink(sink_all());
    check_introspect_simple_processor(source_nothing(sink_all()));
}

TEST_CASE("sink_all") {
    auto sink = sink_all();
    sink.handle(123);
    sink.handle(std::string("hello"));
}

TEST_CASE("source_nothing") {
    auto ctx = context::create();
    auto src = source_nothing(capture_output<type_list<>>(
        ctx->tracker<capture_output_accessor>("out")));
    auto out = ctx->access<capture_output_accessor>("out");
    src.flush();
    REQUIRE(out.check_flushed());
}

namespace {

// Use this instead of sink_all mostly to keep clang-tidy from complaining
// about use of std::move() on a trivially copyable type.
struct nontrivially_copyable_sink : public internal::sink_all {
    std::string some_data;
};

} // namespace

TEST_CASE("source_nothing accepts copyable lvalue or const downstreams") {
    // This demonstrates how the constructor and factory function can allow
    // lvalue, or (as a technicality) const rvalue, downstreams as long as they
    // are copyable. We do not repeat this test for every processor, but
    // parameter passing (for downstreams and auxiliary objects) should follow
    // the same pattern.

    // Calling flush() ensure that type 'Downstream' is not deduced as const.

    SECTION("non-const lvalue") {
        auto sink = nontrivially_copyable_sink();
        auto src = source_nothing(sink);
        src.flush();
    }

    SECTION("const lvalue") {
        auto const sink = nontrivially_copyable_sink();
        auto src = source_nothing(sink);
        src.flush();
    }

    SECTION("non-const rvalue") {
        auto sink = nontrivially_copyable_sink();
        auto src = source_nothing(std::move(sink));
        src.flush();
    }

    SECTION("const rvalue") {
        auto const sink = nontrivially_copyable_sink();
        // NOLINTNEXTLINE(performance-move-const-arg)
        auto src = source_nothing(std::move(sink));
        src.flush();
    }
}

} // namespace tcspc
