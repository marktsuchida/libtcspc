/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>

// Use std::span if C++20 or later.
#if __has_include(<version>)
#include <version> // IWYU pragma: keep
#if __cpp_lib_span >= 202002L
#include <span> // IWYU pragma: export
#define LIBTCSPC_SPAN_USE_STD 1

namespace tcspc {

/**
 * \brief Span, an alias or replacement for \c std::span.
 *
 * \ingroup misc
 *
 * For C++20 and later, this is an alias of `std::span<T, Extent>`.
 * For C++17, this is replaced with a local implementation (tcbrindle/span).
 */
template <class T, std::size_t Extent = std::dynamic_extent>
using span = std::span<T, Extent>;

} // namespace tcspc

#endif // __cpp_lib_span
#endif // <version>

#if not LIBTCSPC_SPAN_USE_STD

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif

#define TCB_SPAN_NAMESPACE_NAME tcspc
#include "tcb/span.hpp" // IWYU pragma: export

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tcspc {

/**
 * \brief Span, an alias or replacement for \c std::span.
 *
 * \ingroup misc
 *
 * For C++20 and later, this is an alias of `std::span<T, Extent>`.
 * For C++17, a local implementation (tcbrindle/span) is used.
 */
template <typename T, std::size_t Extent> class span;

} // namespace tcspc

#endif // not LIBTCSPC_SPAN_USE_STD
