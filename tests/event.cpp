/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/event.hpp"

#include <catch2/catch_test_macros.hpp>

namespace tcspc::internal {

TEST_CASE("abstime_stamped") {
    struct have_not {};
    struct have {
        int abstime;
    };
    STATIC_CHECK_FALSE(abstime_stamped<have_not>);
    STATIC_CHECK(abstime_stamped<have>);
}

} // namespace tcspc::internal
