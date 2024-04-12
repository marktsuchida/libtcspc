/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/view_as_bytes.hpp"

#include "libtcspc/bucket.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace tcspc {

namespace {

using out_events = type_list<bucket<std::byte const>>;

template <typename T> auto tmp_bucket(T &&v) {
    struct ignore_storage {};
    return bucket(span(v), ignore_storage{});
}

} // namespace

TEST_CASE("introspect view_as_bytes", "[introspect]") {
    check_introspect_simple_processor(view_as_bytes(null_sink()));
}

TEST_CASE("view as bytes") {
    auto ctx = std::make_shared<processor_context>();
    auto in =
        feed_input<type_list<int>>(view_as_bytes(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    int const i = 42;
    in.feed(i);
    REQUIRE(out.check(tmp_bucket(as_bytes(span(&i, 1)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("view as bytes, bucket input") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<bucket<int const>>>(
        view_as_bytes(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    std::vector const data{42, 43};
    in.feed(tmp_bucket(data));
    REQUIRE(out.check(tmp_bucket(as_bytes(span(data)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
