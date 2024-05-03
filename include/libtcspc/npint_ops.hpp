/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "npint.hpp"

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace tcspc::internal {

constexpr auto count_trailing_zeros_32_nonintrinsic(u32np x) noexcept -> int {
    int r = 0;
    while ((x & 1_u32np) == 0_u32np) {
        x >>= 1;
        ++r;
    }
    return r;
}

// Return the number of trailing zero bits in x. Behavior is undefined if x is
// zero.
// TODO: In C++20, replace with std::countr_zero()
inline auto count_trailing_zeros_32(u32np x) noexcept -> int {
#ifdef __GNUC__
    return __builtin_ctz(x.value());
#elif defined(_MSC_VER)
    unsigned long r{};
    _BitScanForward(&r, x.value());
    return (int)r;
#else
    return count_trailing_zeros_32_nonintrinsic(x);
#endif
}

template <typename F>
inline void for_each_set_bit(u32np bits, F func) noexcept(noexcept(func(0))) {
    while (bits != 0_u32np) {
        func(count_trailing_zeros_32(bits));
        bits = bits & (bits - 1_u32np); // Clear the handled bit
    }
}

} // namespace tcspc::internal
