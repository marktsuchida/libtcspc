/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace tcspc::internal {

// Disable inlining; Must be placed _after_ standard attributes such as
// [[noreturn]], for MSVC.
#if defined(__GNUC__)
#define LIBTCSPC_NOINLINE [[gnu::noinline]]
#elif defined(_MSC_VER)
// [[msvc::noinline]] requires /std:c++20
#define LIBTCSPC_NOINLINE __declspec(noinline)
#else
#define LIBTCSPC_NOINLINE
#endif

// Run-time check for actual alignment.
template <typename T>
inline auto is_aligned(void const *ptr) noexcept -> bool {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const start = reinterpret_cast<uintptr_t>(ptr);
    return start % alignof(T) == 0;
}

// C++23 std::unreachable(), but safe in debug build.
[[noreturn]] inline void unreachable() {
    assert(false);
#if defined(__GNUC__)
    __builtin_unreachable();
#elif defined(_MSC_VER)
    __assume(false);
#endif
}

// A "false" template metafunction that can be used with static_assert in
// constexpr-if branches (by pretending that it may not always be false).
template <typename T> struct false_for_type : std::false_type {};

template <typename T, typename... U> struct is_any_of {
    static constexpr bool value = (std::is_same_v<T, U> || ...);
};

template <typename T, typename... U>
inline constexpr bool is_any_of_v = is_any_of<T, U...>::value;

// C++20 std::type_identity
template <typename T> struct type_identity {
    using type = T;
};

// C++20 std::remove_cvref[_t]
template <typename T> struct remove_cvref {
    using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T> using remove_cvref_t = typename remove_cvref<T>::type;

// Overloaded idiom for std::visit
template <typename... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts> overloaded(Ts...) -> overloaded<Ts...>;

} // namespace tcspc::internal
