/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/buffer.hpp"

#include "libtcspc/common.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace tcspc {

TEST_CASE("introspect buffer", "[introspect]") {
    check_introspect_simple_processor(buffer<int>(1, null_sink()));
    check_introspect_simple_processor(
        real_time_buffer<int>(1, std::chrono::seconds(1), null_sink()));
    check_introspect_simple_processor(
        single_threaded_buffer<int>(1, null_sink()));
}

} // namespace tcspc
