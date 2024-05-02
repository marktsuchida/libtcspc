/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "introspect.hpp"
#include "npint.hpp"

#include <cassert>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

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
 * \brief Default set of integer data types.
 *
 * Many events and processors in libtcspc deal with multiple integer types, so
 * specifying them individually would be cumbersome. We therefore usually
 * specify them as a single unit called the _data type set_ (usually template
 * parameter `DataTypes`), which is a type containing several type aliases to
 * be used across a processing graph (or part of a processing graph). This is
 * the default data type set.
 *
 * \ingroup misc
 */
struct default_data_types {
    /**
     * \brief Absolute time type.
     *
     * The default of `std::int64_t` is chosen because 64-bit precision is
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
 * \brief An event type indicating a warning.
 *
 * \ingroup events-core
 *
 * Processors that encounter recoverable errors emit this event. It can be used
 * together with `tcspc::stop()` or `tcspc::stop_with_error()` to stop
 * processing.
 *
 * Processors that generate this event should also pass through this event. In
 * this way, multiple warning-emitting processors can be chained before a
 * single point where the warnings are handled.
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
 * \ingroup events-core
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
 * \ingroup processors-core
 *
 * \par Events handled
 * - All types: ignore
 * - Flush: ignore
 */
class null_sink {
  public:
    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "null_sink");
    }

    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    /** \brief Implements processor requirement. */
    template <typename Event>
    void handle([[maybe_unused]] Event const &event) {}

    /** \brief Implements processor requirement. */
    void flush() {}
};

namespace internal {

template <typename Downstream> class null_source {
    bool flushed = false;
    Downstream downstream;

  public:
    explicit null_source(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "null_source");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void flush() {
        if (flushed) {
            throw std::logic_error(
                "null_source may not be flushed a second time");
        }
        flushed = true;
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that sources an empty stream.
 *
 * \ingroup processors-core
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Flush: pass through with no action
 */
template <typename Downstream> auto null_source(Downstream &&downstream) {
    return internal::null_source<Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Histogram overflow policy tag type to request saturating addition on
 * overflowed bins.
 *
 * \ingroup histogram-policies
 */
struct saturate_on_overflow_t {
    explicit saturate_on_overflow_t() = default;
};

/**
 * \brief Histogram overflow policy tag type to request resetting the histogram
 * (array) when a bin is about to overflow.
 *
 * \ingroup histogram-policies
 */
struct reset_on_overflow_t {
    explicit reset_on_overflow_t() = default;
};

/**
 * \brief Histogram overflow policy tag type to request ending the processing
 * when a bin is about to overflow.
 *
 * \ingroup histogram-policies
 */
struct stop_on_overflow_t {
    explicit stop_on_overflow_t() = default;
};

/**
 * \brief Histogram overflow policy tag type to request treating bin overflows
 * as errors.
 *
 * \ingroup histogram-policies
 */
struct error_on_overflow_t {
    explicit error_on_overflow_t() = default;
};

/**
 * \brief Histogram policy tag type to request skipping emission of
 * `tcspc::concluding_histogram_array_event`.
 *
 * \ingroup histogram-policies
 */
struct skip_concluding_event_t {
    explicit skip_concluding_event_t() = default;
};

namespace internal {

struct error_on_overflow_and_skip_concluding_event_t {
    explicit error_on_overflow_and_skip_concluding_event_t() = default;
};

} // namespace internal

/**
 * \brief Histogram overflow policy tag instance to request saturating addition
 * on overflowed bins.
 *
 * \ingroup histogram-policies
 */
inline constexpr saturate_on_overflow_t saturate_on_overflow{};

/**
 * \brief Histogram overflow policy tag instance to request resetting the
 * histogram (array) when a bin is about to overflow.
 *
 * \ingroup histogram-policies
 */
inline constexpr reset_on_overflow_t reset_on_overflow{};

/**
 * \brief Histogram overflow policy tag instance to request ending the
 * processing when a bin is about to overflow.
 *
 * \ingroup histogram-policies
 */
inline constexpr stop_on_overflow_t stop_on_overflow{};

/**
 * \brief Histogram overflow policy tag instance to request treating bin
 * overflows as errors.
 *
 * \ingroup histogram-policies
 *
 * For `tcspc::histogram_elementwise_accumulate`, this value can be combined
 * with `tcspc::skip_concluding_event` using the `|` operator.
 */
inline constexpr error_on_overflow_t error_on_overflow{};

/**
 * \brief Histogram policy tag instance to request skipping emission of
 * `tcspc::concluding_histogram_array_event`.
 *
 * \ingroup histogram-policies
 *
 * This value can be combined with `tcspc::error_on_overflow` using the `|`
 * operator.
 */
inline constexpr skip_concluding_event_t skip_concluding_event{};

namespace internal {

inline constexpr error_on_overflow_and_skip_concluding_event_t
    error_on_overflow_and_skip_concluding_event{};

}

/** \private */
constexpr auto
operator|([[maybe_unused]] error_on_overflow_t const &lhs,
          [[maybe_unused]] skip_concluding_event_t const &rhs) noexcept {
    return internal::error_on_overflow_and_skip_concluding_event;
}

/** \private */
constexpr auto
operator|([[maybe_unused]] skip_concluding_event_t const &lhs,
          [[maybe_unused]] error_on_overflow_t const &rhs) noexcept {
    return internal::error_on_overflow_and_skip_concluding_event;
}

namespace internal {

[[noreturn]] inline void unreachable() {
    // C++23: std::unreachable(), but safe in debug build.
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

template <typename T>
inline auto is_aligned(void const *ptr) noexcept -> bool {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const start = reinterpret_cast<uintptr_t>(ptr);
    return start % alignof(T) == 0;
}

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

} // namespace internal

} // namespace tcspc
