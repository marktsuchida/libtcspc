/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <numeric>
#include <ostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

/**
 * \brief Event representing result of fitting an arithmetic time sequence.
 *
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct start_and_interval_event {
    /**
     * \brief Absolute time of the fitted intercept, rounede to integer.
     */
    typename DataTraits::abstime_type abstime;

    /**
     * \brief Intercept (interval in abstime units per index) of the
     * arithmetic sequence.
     */
    double interval;

    /** \brief Equality comparison operator. */
    friend auto operator==(start_and_interval_event const &lhs,
                           start_and_interval_event const &rhs) noexcept {
        return lhs.abstime == rhs.abstime && lhs.interval == rhs.interval;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(start_and_interval_event const &lhs,
                           start_and_interval_event const &rhs) noexcept {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           start_and_interval_event const &event)
        -> std::ostream & {
        return stream << "start_and_interval(" << event.abstime << ", "
                      << event.interval << ')';
    }
};

namespace internal {

struct linear_fit_result {
    double intercept;
    double slope;
    double mse; // mean squared error
};

// y.size() must be at least 3. If it is 2, mse will be NaN. If it is 0 or 1,
// all results will be NaN.
inline auto linear_fit_sequence(std::vector<double> const &y)
    -> linear_fit_result {
    // Linear fit: yfit = a + b * x; y from data sequence, x from fixed
    // indices:
    //
    //     [  y_0]        [1    0]        [  0]
    //     [  y_1]        [1    1]        [  1]
    //     [  y_1]        [1    2]        [  2]
    // y = [    .],   X = [.    .],   x = [  .]
    //     [    .]        [.    .]        [  .]
    //     [    .]        [.    .]        [  .]
    //     [y_n-1]        [1  n-1]        [n-1]

    std::uint64_t const n = y.size();

    std::vector<double> x(n);
    std::iota(x.begin(), x.end(), 0.0);

    // Sum of 0, 1, ..., n - 1
    std::uint64_t const isigma_x = (n - 1) * n / 2;
    auto const sigma_x = static_cast<double>(isigma_x);

    // Sum of 0^2, 1^2, ..., (n - 1)^2
    std::uint64_t const isigma_xx = isigma_x * (2 * (n - 1) + 1) / 3;
    auto const sigma_xx = static_cast<double>(isigma_xx);

    // Sum of y_0, y_1, ..., y_n-1
    double const sigma_y = std::reduce(y.cbegin(), y.cend());

    // Sum of x_0 * y_0, x_1 * y_1, ..., x_n-1 * y_n-1
    double const sigma_xy =
        std::transform_reduce(x.cbegin(), x.cend(), y.cbegin(), 0.0);

    // Determinant of Xt X
    double const det_XtX =
        static_cast<double>(n) * sigma_xx - sigma_x * sigma_x;

    // Solve ordinary linear least squares:
    // [a b]t = (Xt X)^-1 Xt y
    double const a = (sigma_xx * sigma_y - sigma_x * sigma_xy) / det_XtX;
    double const b =
        (static_cast<double>(n) * sigma_xy - sigma_x * sigma_y) / det_XtX;

    // Sum of squared residuals
    double const ssr =
        std::transform_reduce(x.cbegin(), x.cend(), y.cbegin(), 0.0,
                              std::plus<>(), [&](double x, double y) noexcept {
                                  auto const yfit = a + b * x;
                                  auto const r = y - yfit;
                                  return r * r;
                              });
    double const mse = ssr / static_cast<double>(n - 2);

    return {a, b, mse};
}

template <typename DataTraits, typename Event, typename Downstream>
class fit_arithmetic_time_sequence {
    using abstime_type = typename DataTraits::abstime_type;

    std::size_t len; // At least 3
    abstime_type min_interval_cutoff;
    abstime_type max_interval_cutoff;
    double mse_cutoff;

    // Record times relative to first event of the series, to prevent overflow
    // or loss of precision on large abstime values.
    abstime_type first_tick_time{};
    std::vector<double> relative_ticks; // First element will be 0.0.

    Downstream downstream;

    void fail(std::string const &message) {
        throw std::runtime_error("fit arithmetic time sequence: " + message);
    }

  public:
    explicit fit_arithmetic_time_sequence(
        std::size_t length,
        std::array<typename DataTraits::abstime_type, 2> min_max_interval,
        double max_mse, Downstream &&downstream)
        : len(length), min_interval_cutoff(min_max_interval[0]),
          max_interval_cutoff(min_max_interval[1]), mse_cutoff(max_mse),
          downstream(std::move(downstream)) {
        if (length < 3)
            throw std::invalid_argument(
                "fit_arithmetic_time_sequence length must be at least 3");
        relative_ticks.reserve(length);
    }

    void handle(Event const &event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);

        if (relative_ticks.empty())
            first_tick_time = event.abstime;

        relative_ticks.push_back(
            static_cast<double>(event.abstime - first_tick_time));

        if (relative_ticks.size() == len) {
            linear_fit_result result{};
            result = linear_fit_sequence(relative_ticks);

            if (result.mse > mse_cutoff)
                return fail("mean squared error exceeded cutoff");
            if (result.slope < double(min_interval_cutoff) ||
                result.slope > double(max_interval_cutoff))
                return fail(
                    "estimated time interval was not in expected range");

            auto const rounded_intercept = std::llround(result.intercept);
            if constexpr (std::is_unsigned_v<abstime_type>) {
                if (rounded_intercept < 0 &&
                    static_cast<abstime_type>(-rounded_intercept) >
                        first_tick_time)
                    return fail(
                        "estimated start time is negative but abstime_type is unsigned");
            }
            auto const abstime =
                rounded_intercept < 0
                    ? first_tick_time -
                          static_cast<abstime_type>(-rounded_intercept)
                    : first_tick_time +
                          static_cast<abstime_type>(rounded_intercept);
            auto max_time_shift =
                max_interval_cutoff * static_cast<abstime_type>(len);
            if constexpr (std::is_unsigned_v<abstime_type>) {
                if (max_time_shift > event.abstime) {
                    // The case of negative abstime is already checked above,
                    // so effectively disable the max-time-shift check.
                    max_time_shift = event.abstime;
                }
            }
            if (abstime < event.abstime - max_time_shift)
                return fail(
                    "estimated start time was earlier than guaranteed time bound");

            downstream.handle(
                start_and_interval_event<DataTraits>{abstime, result.slope});
            relative_ticks.clear();
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that fits an arithmetic sequence to the abstimes
 * of an event.
 *
 * \ingroup processors-timing
 *
 * The processor accepts a single event type, \c Event. Every \e length events
 * are grouped together and their abstime is fit to an arithmetic sequence
 * (that is, a sequence of equally spaced values). If the fit is successful
 * (see below), then a \c start_and_interval_event is emitted, whose abstime is
 * the estimated time of the first event (rounded to integer) and which
 * contains the estimated event interval. If the fit is not successful, an
 * error is generated.
 *
 * The fit is considered successful if all of the following conditions are
 * satisfied:
 *
 * -# the mean squared error is no more than \e max_mse
 * -# the estimated event interval is within \e min_max_interval
 * -# the estimated time of the first event (rounded to integer) is not less
 *    than the observed time of the last event minus <tt>length *
 *    min_max_interval[1]</tt>
 *
 * (The last criterion provides a bound for the time shift if the emitted
 * events are merged back to the origianl event stream from which the input
 * events were derived, because the \c start_and_interval_event is emitted when
 * handling the last event.)
 *
 * This processor does not pass through \c Event, and does not handle any other
 * event (because such events would be out of order with the \c
 * start_and_interval_event events).
 *
 * \tparam DataTraits traits type specifying data types for emitted event
 *
 * \tparam Event event whose timing is to be fit
 *
 * \tparam Downstream downstream processor type
 *
 * \param length number of Event events to fit
 *
 * \param min_max_interval allowed range of estimated event interval for the
 * fit to be considered successful
 *
 * \param max_mse allowed maximum mean squared error for the fit to be
 * considered successful
 *
 * \param downstream downstream processor
 */
template <typename DataTraits, typename Event, typename Downstream>
auto fit_arithmetic_time_sequence(
    std::size_t length,
    std::array<typename DataTraits::abstime_type, 2> min_max_interval,
    double max_mse, Downstream &&downstream) {
    return internal::fit_arithmetic_time_sequence<DataTraits, Event,
                                                  Downstream>(
        length, min_max_interval, max_mse,
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
