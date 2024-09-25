/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/event_traits.hpp"

#include <catch2/catch_test_macros.hpp>

namespace tcspc::internal {

TEST_CASE("has_abstime") {
    struct have_not {};
    struct have {
        int abstime;
    };
    STATIC_CHECK_FALSE(has_abstime_v<have_not>);
    STATIC_CHECK(has_abstime_v<have>);
}

} // namespace tcspc::internal
