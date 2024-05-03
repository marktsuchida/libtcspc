/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace tcspc {

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

} // namespace internal

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

} // namespace tcspc
