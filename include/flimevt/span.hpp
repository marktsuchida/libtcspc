/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <gsl/span>

#include <cstddef>

namespace flimevt {

/**
 * \brief Span, similar to \c std::span.
 *
 * Currently, this is an alias for \c gsl::span. This may be replaced with
 * something compatible with C++20 \c std::span in the future.
 */
template <class T, std::size_t Extent = gsl::dynamic_extent>
using span = gsl::span<T, Extent>;

} // namespace flimevt
