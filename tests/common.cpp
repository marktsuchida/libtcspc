/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/common.hpp"

#include <catch2/catch_test_macros.hpp>

namespace tcspc::internal {

TEST_CASE("false_for_type") { STATIC_CHECK_FALSE(false_for_type<int>::value); }

} // namespace tcspc::internal
