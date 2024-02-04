/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/buffer.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <memory>
#include <vector>

namespace tcspc {

TEST_CASE("introspect buffer", "[introspect]") {
    check_introspect_simple_processor(
        dereference_pointer<void *>(null_sink()));
    check_introspect_simple_processor(batch<int, std::vector<int>>(
        std::shared_ptr<object_pool<std::vector<int>>>{}, 1, null_sink()));
    check_introspect_simple_processor(
        unbatch<std::vector<int>, int>(null_sink()));
    check_introspect_simple_processor(buffer<int>(1, null_sink()));
    check_introspect_simple_processor(
        real_time_buffer<int>(1, std::chrono::seconds(1), null_sink()));
    check_introspect_simple_processor(
        single_threaded_buffer<int>(1, null_sink()));
}

TEST_CASE("object pool") {
    auto pool = std::make_shared<object_pool<int>>(1, 3);
    auto o = pool->check_out(); // count == 1
    auto p = pool->check_out(); // count == 2
    auto q = pool->check_out(); // count == 3
    // Hard to test blocking when reached max count. Test only non-blocking
    // cases.
    o = {};                     // Check-in; count == 2
    auto r = pool->check_out(); // count == 3
    auto s = pool->try_ckeck_out();
    CHECK_FALSE(s);
}

TEST_CASE("batch") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<int>>(batch<int, pvector<int>>(
        std::make_shared<object_pool<pvector<int>>>(), 3,
        dereference_pointer<std::shared_ptr<pvector<int>>>(
            capture_output<type_list<pvector<int>>>(
                ctx->tracker<capture_output_accessor>("out")))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<pvector<int>>>(
        ctx->accessor<capture_output_accessor>("out"));

    SECTION("ending mid-batch") {
        in.feed(42);
        in.feed(43);
        in.feed(44);
        REQUIRE(out.check(pvector<int>{42, 43, 44}));
        in.feed(45);
        in.flush();
        REQUIRE(out.check(pvector<int>{45}));
        REQUIRE(out.check_flushed());
    }

    SECTION("ending in full batch") {
        in.feed(42);
        in.feed(43);
        in.feed(44);
        REQUIRE(out.check(pvector<int>{42, 43, 44}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("unbatch") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<pvector<int>>>(
        unbatch<pvector<int>, int>(capture_output<type_list<int>>(
            ctx->tracker<capture_output_accessor>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(
        ctx->accessor<capture_output_accessor>("out"));

    in.feed(pvector<int>{42, 43, 44});
    REQUIRE(out.check(42));
    REQUIRE(out.check(43));
    REQUIRE(out.check(44));
    in.feed(pvector<int>{});
    in.feed(pvector<int>{});
    in.feed(pvector<int>{45});
    REQUIRE(out.check(45));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
