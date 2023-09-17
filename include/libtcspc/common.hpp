/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "npint.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#if defined(__GNUC__)
#define LIBTCSPC_NOINLINE [[gnu::noinline]]
#elif defined(_MSC_VER)
// [[msvc::noinline]] requires /std:c++20
#define LIBTCSPC_NOINLINE __declspec(noinline)
#else
#define LIBTCSPC_NOINLINE
#endif

namespace tcspc {

/**
 * \brief Default traits for integer data types.
 *
 * \ingroup misc
 */
struct default_data_traits {
    /**
     * \brief Absolute time type.
     *
     * The default of \c int64_t is chosen because 64-bit precision is
     * reasonable (32-bit would overflow; 128-bit would hurt performance and is
     * not required for most applications) and because we want to allow
     * negative time stamps.
     */
    using abstime_type = std::int64_t;

    /**
     * \brief Channel number type.
     */
    using channel_type = std::int32_t;

    /**
     * \brief Difference time type.
     */
    using difftime_type = std::int32_t;

    /**
     * \brief Type of datapoint for histogramming.
     */
    using datapoint_type = std::int32_t;

    /**
     * \brief Type of histogram bin index.
     */
    using bin_index_type = std::uint16_t;

    /**
     * \brief Type of histogram bin value (count).
     */
    using bin_type = std::uint16_t;
};

/**
 * \brief Exception type used to indicate processor-initiated non-error end of
 * processing.
 */
class end_processing final : public std::exception {
    std::string msg;

  public:
    /**
     * \brief Construct with status message.
     *
     * \param message the message, which should describe the reason for the end
     * of processing
     */
    explicit end_processing(std::string message) : msg(std::move(message)) {}

    /** \brief std::exception interface. */
    [[nodiscard]] auto what() const noexcept -> char const * override {
        return msg.c_str();
    }
};

/**
 * \brief An event type indicating a warning.
 *
 * \ingroup events-basic
 *
 * Some processors that perform checks on data may emit this event. It can be
 * used together with \ref stop_with_error to stop processing.
 *
 * Processors that generate this event should also pass through this event. In
 * this way, multiple check processors can be chained before a single point
 * where the warnings are handled.
 */
struct warning_event {
    /** \brief A human-readable message describing the warning. */
    std::string message;

    /** \brief Equality comparison operator. */
    friend auto operator==(warning_event const &lhs,
                           warning_event const &rhs) noexcept -> bool {
        return lhs.message == rhs.message;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(warning_event const &lhs,
                           warning_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream, warning_event const &event)
        -> std::ostream & {
        return stream << event.message;
    }
};

/**
 * \brief An event type whose instances never occur.
 *
 * \ingroup events-basic
 *
 * This can be used to configure unused inputs to processors.
 */
struct never_event {
    never_event() = delete;
};

/**
 * \brief Processor that sinks any event and the end-of-stream and does
 * nothing.
 *
 * \ingroup processors-basic
 */
class null_sink {
  public:
    /** \brief Processor interface */
    template <typename Event>
    void handle([[maybe_unused]] Event const &event) {}

    /** \brief Processor interface */
    void flush() {}
};

namespace internal {

template <typename Downstream> class null_source {
    Downstream downstream;

  public:
    explicit null_source(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void pump_events() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that sources an empty stream.
 *
 * \ingroup processors-basic
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return null source having \c pump_events member function
 */
template <typename Downstream> auto null_source(Downstream &&downstream) {
    return internal::null_source<Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Histogram overflow strategy tag to request saturating addition on
 * overflowed bins.
 *
 * \ingroup overflow-strategies
 */
struct saturate_on_overflow {
    explicit saturate_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request resetting the histogram
 * when a bin is about to overflow.
 *
 * \ingroup overflow-strategies
 */
struct reset_on_overflow {
    explicit reset_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request ending the processing when
 * a bin is about to overflow.
 *
 * \ingroup overflow-strategies
 */
struct stop_on_overflow {
    explicit stop_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request treating bin overflows as
 * errors.
 *
 * \ingroup overflow-strategies
 */
struct error_on_overflow {
    explicit error_on_overflow() = default;
};

/**
 * \brief Error raised when a histogram bin overflows.
 *
 * \ingroup exceptions
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
 * \ingroup exceptions
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

template <typename T, typename... U> struct is_any_of {
    static constexpr bool value = (std::is_same_v<T, U> || ...);
};

template <typename T, typename... U>
inline constexpr bool is_any_of_v = is_any_of<T, U...>::value;

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
inline constexpr auto as_signed(T i) -> std::make_signed_t<T> {
    return static_cast<std::make_signed_t<T>>(i);
}

template <typename T, typename = std::enable_if_t<std::is_signed_v<T>>>
inline constexpr auto as_unsigned(T i) -> std::make_unsigned_t<T> {
    return static_cast<std::make_unsigned_t<T>>(i);
}

template <typename T, typename U> inline auto narrow(U i) -> T {
    static_assert(std::is_integral_v<T>);
    static_assert(std::is_integral_v<U>);
    static_assert(std::is_signed_v<T> == std::is_signed_v<U>);
    static_assert(sizeof(T) < sizeof(U));
    assert(i <= std::numeric_limits<T>::max());
    if constexpr (std::is_signed_v<T>) {
        assert(i >= std::numeric_limits<T>::min());
    }
    return T(i);
}

// window_size must be non-negative
template <typename T>
constexpr auto pairing_cutoff(T stop_time, T window_size) noexcept {
    // Guard against underflow (window_size is non-negative).
    if (stop_time < std::numeric_limits<T>::min() + window_size)
        return std::numeric_limits<T>::min();
    return stop_time - window_size;
}

} // namespace internal

} // namespace tcspc
