/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/buffer.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

#include <memory>

namespace tcspc {

TEST_CASE("object pool", "[object_pool]") {
    auto pool = std::make_shared<object_pool<int>>(1, 3);
    auto o = pool->check_out(); // count == 1
    auto p = pool->check_out(); // count == 2
    auto q = pool->check_out(); // count == 3
    // Hard to test blocking when reached max count. Test only non-blocking
    // cases.
    o = {};                     // Check-in; count == 2
    auto r = pool->check_out(); // count == 3
    auto s = pool->maybe_check_out();
    CHECK_FALSE(s);
}

TEST_CASE("batch", "[batch]") {
    auto out = capture_output<event_set<pvector<int>>>();
    auto in = feed_input<event_set<int>>(batch<int, pvector<int>>(
        std::make_shared<object_pool<pvector<int>>>(), 3,
        dereference_pointer<std::shared_ptr<pvector<int>>>(
            ref_processor(out))));
    in.require_output_checked(out);

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

TEST_CASE("unbatch", "[unbatch]") {
    auto out = capture_output<event_set<int>>();
    auto in = feed_input<event_set<pvector<int>>>(
        unbatch<pvector<int>, int>(ref_processor(out)));
    in.require_output_checked(out);

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
