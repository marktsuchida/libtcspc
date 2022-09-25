/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <stdexcept>
#include <type_traits>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace flimevt {

/**
 * \brief Signed 64-bit integer type representing macrotime.
 *
 * The macrotime is the monotonically increasing timestamp assigned to events
 * by time tagging hardware, after processing to eliminate wraparounds.
 *
 * We used a signed integer type because negative times can arise (for example
 * if a negative delay is applied to events).
 *
 * The physical units of the macrotime is dependent on the input data and it is
 * the user's responsibility to interpret correctly. FLIMEvents is designed to
 * use integer values without scaling and does not handle physical units.
 *
 * It is assumed that macrotime values never overflow. The maximum
 * representable value is over 9E18. If the macrotime units are picoseconds,
 * this corresponds to about 3 and a half months.
 */
using macrotime = std::int64_t;

/**
 * \brief An event type whose instances never occur.
 *
 * This can be used to configure unused inputs to processors.
 */
class never_event {
    never_event() = delete;
    never_event(never_event const &) = delete;
    never_event &operator=(never_event const &) = delete;
};

/**
 * \brief Histogram overflow strategy tag to request saturating addition on
 * overflowed bins.
 */
struct saturate_on_overflow {
    explicit saturate_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request resetting the histogram
 * when a bin is about to overflow.
 */
struct reset_on_overflow {
    explicit reset_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request ending the processing when
 * a bin is about to overflow.
 */
struct stop_on_overflow {
    explicit stop_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request treating bin overflows as
 * errors.
 */
struct error_on_overflow {
    explicit error_on_overflow() = default;
};

/**
 * \brief Error raised when a histogram bin overflows.
 *
 * This error is raised when the error_on_overflow strategy is requested and
 * there was an overflow. It is also raised when reset_on_overflow is requested
 * but a reset would result in an infinite loop: in the case of histogram if
 * maximum per bin set to 0, or accumulate_histograms if a single batch
 * contains enough increments to overflow a bin.
 */
class histogram_overflow_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Error raised when histogram array cycle is incomplete.
 *
 * All but the last cycle before a reset or end-of-stream must be complete for
 * processors computing histogram arrays. This exception is thrown if a
 * new-cycle event is received before the current cycle has had the expected
 * number of batches.
 */
class incomplete_array_cycle_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

namespace internal {

[[noreturn]] inline void unreachable() {
    // C++23: std::unreachable()
#if defined(__GNUC__)
    __builtin_unreachable();
#elif defined(_MSC_VER)
    __assume(false);
#endif
}

// A "false" template metafunction that can be used with static_assert in
// constexpr-if branches (by pretending that it may not always be false).
template <typename T> struct false_for_type : std::false_type {};

constexpr int count_trailing_zeros_32_nonintrinsic(std::uint32_t x) noexcept {
    int r = 0;
    while ((x & 1) == 0) {
        x >>= 1;
        ++r;
    }
    return r;
}

// Return the number of trailing zero bits in x. Behavior is undefined if x is
// zero.
// TODO: In C++20, replace with std::countr_zero()
inline int count_trailing_zeros_32(std::uint32_t const x) noexcept {
#ifdef __GNUC__
    return __builtin_ctz(x);
#elif defined(_MSC_VER)
    unsigned long r;
    _BitScanForward(&r, x);
    return (int)r;
#else
    return count_trailing_zeros_32_nonintrinsic(x);
#endif
}

} // namespace internal

} // namespace flimevt
