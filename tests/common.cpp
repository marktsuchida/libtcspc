/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/common.hpp"

#include <catch2/catch_test_macros.hpp>

namespace tcspc::internal {

TEST_CASE("always_false") { STATIC_CHECK_FALSE(always_false_v<int>); }

} // namespace tcspc::internal
