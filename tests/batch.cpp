/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/batch.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/object_pool.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace tcspc {

TEST_CASE("introspect batch, unbatch", "[introspect]") {
    check_introspect_simple_processor(batch<int, std::vector<int>>(
        std::shared_ptr<object_pool<std::vector<int>>>{}, 1, null_sink()));
    check_introspect_simple_processor(unbatch<int>(null_sink()));
}

TEST_CASE("batch") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<int>>(batch<int, pvector<int>>(
        std::make_shared<object_pool<pvector<int>>>(), 3,
        dereference_pointer<std::shared_ptr<pvector<int>>>(
            capture_output<type_list<pvector<int>>>(
                ctx->tracker<capture_output_access>("out")))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<pvector<int>>>(
        ctx->access<capture_output_access>("out"));

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
        unbatch<int>(capture_output<type_list<int>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(
        ctx->access<capture_output_access>("out"));

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

TEST_CASE("process_in_batches") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<int>>(process_in_batches<int>(
        3, capture_output<type_list<int>>(
               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(
        ctx->access<capture_output_access>("out"));

    in.feed(42);
    in.feed(43);
    in.feed(44);
    REQUIRE(out.check(42));
    REQUIRE(out.check(43));
    REQUIRE(out.check(44));
    in.feed(45);
    in.flush();
    REQUIRE(out.check(45));
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
