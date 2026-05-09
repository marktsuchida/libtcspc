/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "npint.hpp"

#include <bit>

namespace tcspc::internal {

template <typename F>
inline void for_each_set_bit(u32np bits, F func) noexcept(noexcept(func(0))) {
    while (bits != 0_u32np) {
        func(std::countr_zero(bits.value()));
        bits = bits & (bits - 1_u32np); // Clear the handled bit
    }
}

} // namespace tcspc::internal
