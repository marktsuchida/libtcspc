/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <concepts>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc::internal {

constexpr auto as_signed(std::unsigned_integral auto i)
    -> std::make_signed_t<decltype(i)> {
    return static_cast<std::make_signed_t<decltype(i)>>(i);
}

constexpr auto as_unsigned(std::signed_integral auto i)
    -> std::make_unsigned_t<decltype(i)> {
    return static_cast<std::make_unsigned_t<decltype(i)>>(i);
}

constexpr auto ensure_signed(std::integral auto i)
    -> std::make_signed_t<decltype(i)> {
    return static_cast<std::make_signed_t<decltype(i)>>(i);
}

constexpr auto ensure_unsigned(std::integral auto i)
    -> std::make_unsigned_t<decltype(i)> {
    return static_cast<std::make_unsigned_t<decltype(i)>>(i);
}

// Whether the conversion T -> R is non-narrowing (every value of T is
// representable in R).
template <typename T, typename R>
concept representable_in =
    std::integral<T> && std::integral<R> &&
    std::cmp_greater_equal(std::numeric_limits<T>::min(),
                           std::numeric_limits<R>::min()) &&
    std::cmp_less_equal(std::numeric_limits<T>::max(),
                        std::numeric_limits<R>::max());

template <typename R, std::integral T>
constexpr auto convert_with_check(T v) -> R {
    if (not std::in_range<R>(v))
        throw std::range_error("value out of range of integer type");
    return static_cast<R>(v);
}

template <std::integral T> constexpr auto add_with_check(T a, T b) -> T {
#ifdef __GNUC__
    T c{};
    if (not __builtin_add_overflow(a, b, &c))
        return c;
#else
    using limits = std::numeric_limits<T>;
    bool const safe_to_add = std::is_signed_v<T> && b < 0
                                 ? a >= limits::min() - b
                                 : a <= limits::max() - b;
    if (safe_to_add)
        return a + b;
#endif
    throw std::overflow_error("integer overflow on addition");
}

template <std::integral T> constexpr auto subtract_with_check(T a, T b) -> T {
#ifdef __GNUC__
    T c{};
    if (not __builtin_sub_overflow(a, b, &c))
        return c;
#else
    using limits = std::numeric_limits<T>;
    bool const safe_to_subtract = std::is_signed_v<T> && b < 0
                                      ? a <= limits::max() + b
                                      : a >= limits::min() + b;
    if (safe_to_subtract)
        return a - b;
#endif
    throw std::overflow_error("integer overflow on subtraction");
}

// Cf. C++26 std::add_sat()
template <std::integral T> constexpr auto add_sat(T a, T b) noexcept -> T {
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
    if constexpr (std::is_signed_v<T>) {
        if (a < 0)
            return limits::min();
    }
    return limits::max();
}

template <std::integral T>
constexpr auto add_with_wrap(T a, T b) noexcept -> T {
    return static_cast<T>(as_unsigned(a) + as_unsigned(b));
}

template <std::integral T>
constexpr auto subtract_with_wrap(T a, T b) noexcept -> T {
    return static_cast<T>(as_unsigned(a) - as_unsigned(b));
}

// The earliest abstime that is still within window_size of stop_time (and
// within T's limits). Requires window_size >= 0.
template <std::integral T>
constexpr auto pairing_cutoff(T stop_time, T window_size) noexcept {
    // Guard against underflow (window_size is non-negative).
    if (stop_time < std::numeric_limits<T>::min() + window_size)
        return std::numeric_limits<T>::min();
    return stop_time - window_size;
}

} // namespace tcspc::internal
