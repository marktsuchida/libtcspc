/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/object_pool.hpp"

#include "libtcspc/common.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace tcspc {

TEST_CASE("introspect dereference_pointer", "[introspect]") {
    check_introspect_simple_processor(
        dereference_pointer<void *>(null_sink()));
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

} // namespace tcspc
