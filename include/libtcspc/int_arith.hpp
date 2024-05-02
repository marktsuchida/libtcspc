/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <limits>
#include <type_traits>

namespace tcspc::internal {

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
inline constexpr auto as_signed(T i) -> std::make_signed_t<T> {
    return static_cast<std::make_signed_t<T>>(i);
}

template <typename T, typename = std::enable_if_t<std::is_signed_v<T>>>
inline constexpr auto as_unsigned(T i) -> std::make_unsigned_t<T> {
    return static_cast<std::make_unsigned_t<T>>(i);
}

// Cf. C++26 std::add_sat()
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
constexpr auto add_sat(T a, T b) noexcept -> T {
    using limits = std::numeric_limits<T>;
#ifdef __GNUC__
    T c{};
    if (not __builtin_add_overflow(a, b, &c))
        return c;
#else
    bool const safe_to_add = std::is_signed_v<T> && b < 0
                                 ? a >= limits::min() - b
                                 : a <= limits::max() - b;
    if (safe_to_add)
        return a + b;
#endif
    if (std::is_signed_v<T> && a < 0)
        return limits::min();
    return limits::max();
}

// window_size must be non-negative
template <typename T>
constexpr auto pairing_cutoff(T stop_time, T window_size) noexcept {
    // Guard against underflow (window_size is non-negative).
    if (stop_time < std::numeric_limits<T>::min() + window_size)
        return std::numeric_limits<T>::min();
    return stop_time - window_size;
}

} // namespace tcspc::internal
