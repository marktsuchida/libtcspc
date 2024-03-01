/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if __has_include(<version>)
#include <version> // IWYU pragma: keep
#if __cpp_lib_span >= 202002L
#include <span> // IWYU pragma: export
#define LIBTCSPC_SPAN_USE_STD

namespace tcspc {

using std::as_bytes;
using std::as_writable_bytes;
using std::dynamic_extent;
using std::span;

} // namespace tcspc

#endif // __cpp_lib_span
#endif // <version>

#ifndef LIBTCSPC_SPAN_USE_STD

#include <cstddef>

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
 * \brief Shim for \c std::span.
 *
 * \ingroup misc
 *
 * For C++20 and later, this is just `using std::span`. For C++17, a local
 * implementation is provided.
 */
template <typename T, std::size_t Extent> class span;

} // namespace tcspc

#endif // ndef LIBTCSPC_SPAN_USE_STD
