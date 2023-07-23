/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/buffer.hpp"

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

} // namespace tcspc
