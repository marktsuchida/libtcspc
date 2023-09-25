/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Use std::span if C++20 or later.
#if __has_include(<version>)
#include <version>
#if __cpp_lib_span >= 202002L
#include <span>
#define LIBTCSPC_SPAN_USE_STD 1

namespace tcspc {

/**
 * \brief Span, an alias or replacement for \c std::span.
 *
 * \ingroup misc
 *
 * For C++20 and later, this is an alias of \c std::span.
 * For C++17, this is replaced with a local implementation (tcbrindle/span).
 */
template <class T, std::size_t Extent = std::dynamic_extent>
using span = std::span<T, Extent>;

} // namespace tcspc

#endif // __cpp_lib_span
#endif // <version>

#if not LIBTCSPC_SPAN_USE_STD

#define TCB_SPAN_NAMESPACE_NAME tcspc

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif

#include "tcb/span.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif
