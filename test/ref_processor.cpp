/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/ref_processor.hpp"

#include "libtcspc/common.hpp"
#include "test_checkers.hpp"

namespace tcspc {

TEST_CASE("introspect ref_processor", "[introspect]") {
    auto const s = null_sink();
    check_introspect_simple_processor(ref_processor(s));
}

} // namespace tcspc
