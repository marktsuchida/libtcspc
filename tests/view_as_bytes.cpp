/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/view_as_bytes.hpp"

#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace tcspc {

namespace {

using out_events = type_list<bucket<std::byte const>>;

template <typename T>
auto tmp_bucket(T &&v) { // NOLINT(cppcoreguidelines-missing-std-forward)
    struct ignore_storage {};
    return bucket(span(v), ignore_storage{});
}

} // namespace

TEST_CASE("type constraints: view_as_bytes") {
    using proc_type =
        decltype(view_as_bytes(sink_events<bucket<std::byte const>>()));
    STATIC_CHECK(is_processor_v<proc_type, int, double>);
}

TEST_CASE("introspect: view_as_bytes") {
    check_introspect_simple_processor(view_as_bytes(null_sink()));
}

TEST_CASE("view as bytes") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, view_as_bytes(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    int const i = 42;
    in.handle(i);
    REQUIRE(out.check(emitted_as::always_lvalue,
                      tmp_bucket(as_bytes(span(&i, 1)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("view as bytes, bucket input") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, view_as_bytes(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    std::vector const data{42, 43};
    in.handle(tmp_bucket(data));
    REQUIRE(out.check(emitted_as::always_lvalue,
                      tmp_bucket(as_bytes(span(data)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
