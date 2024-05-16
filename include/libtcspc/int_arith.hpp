/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace tcspc::internal {

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
constexpr auto as_signed(T i) -> std::make_signed_t<T> {
    return static_cast<std::make_signed_t<T>>(i);
}

template <typename T, typename = std::enable_if_t<std::is_signed_v<T>>>
constexpr auto as_unsigned(T i) -> std::make_unsigned_t<T> {
    return static_cast<std::make_unsigned_t<T>>(i);
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
constexpr auto ensure_signed(T i) -> std::make_signed_t<T> {
    return static_cast<std::make_signed_t<T>>(i);
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
constexpr auto ensure_unsigned(T i) -> std::make_unsigned_t<T> {
    return static_cast<std::make_unsigned_t<T>>(i);
}

// Cf. C++20 std::cmp_less(), etc.
template <typename T, typename U>
[[nodiscard]] constexpr auto cmp_less(T t, U u) noexcept -> bool {
    static_assert(std::is_integral_v<T>);
    static_assert(std::is_integral_v<U>);
    if constexpr (std::is_unsigned_v<T> == std::is_unsigned_v<U>)
        return t < u;
    else if constexpr (std::is_signed_v<T>)
        return t < 0 || as_unsigned(t) < u;
    else // U is signed:
        return u >= 0 && t < as_unsigned(u);
}

template <typename T, typename U>
[[nodiscard]] constexpr auto cmp_greater(T t, U u) noexcept -> bool {
    return cmp_less(u, t);
}

template <typename T, typename U>
[[nodiscard]] constexpr auto cmp_less_equal(T t, U u) noexcept -> bool {
    return not cmp_less(u, t);
}

template <typename T, typename U>
[[nodiscard]] constexpr auto cmp_greater_equal(T t, U u) noexcept -> bool {
    return not cmp_less(t, u);
}

// Statically check for non-narrowing conversion.
template <typename R, typename T>
[[nodiscard]] constexpr auto is_type_in_range([[maybe_unused]] T i) noexcept
    -> bool {
    return cmp_greater_equal(std::numeric_limits<T>::min(),
                             std::numeric_limits<R>::min()) &&
           cmp_less_equal(std::numeric_limits<T>::max(),
                          std::numeric_limits<R>::max());
}

// Cf. C++20 std::in_range()
template <typename R, typename T>
[[nodiscard]] constexpr auto in_range(T i) noexcept -> bool {
    if constexpr (is_type_in_range<R>(T{0}))
        return true;
    return cmp_greater_equal(i, std::numeric_limits<R>::min()) &&
           cmp_less_equal(i, std::numeric_limits<R>::max());
}

template <typename R, typename T,
          typename = std::enable_if_t<std::is_integral_v<T>>>
constexpr auto convert_with_check(T v) -> R {
    if (not in_range<R>(v))
        throw std::range_error("value out of range of integer type");
    return static_cast<R>(v);
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
auto add_with_check(T a, T b) -> T {
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

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
auto subtract_with_check(T a, T b) -> T {
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

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
constexpr auto add_with_wrap(T a, T b) noexcept -> T {
    return static_cast<T>(as_unsigned(a) + as_unsigned(b));
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
constexpr auto subtract_with_wrap(T a, T b) noexcept -> T {
    return static_cast<T>(as_unsigned(a) - as_unsigned(b));
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
