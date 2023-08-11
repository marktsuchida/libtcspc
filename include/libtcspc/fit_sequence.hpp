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

// Design note: It is important that we use double, not float, for these
// computations. The abstime units might be picoseconds, and the event interval
// being fit might be several microseconds (typical pixel clock) with a
// sequence length of up to ~1000. Under these conditions (9-10 orders of
// magnitude between unit and total), even with the use of relative time values
// (as we do), float may lose precision before the end of a single sequence is
// reached.

/**
 * \brief Event representing result of fitting a periodic sequence.
 *
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct start_and_interval_event {
    /**
     * \brief Absolute time of the fitted intercept (time of first event of the
     * sequence), rounede to integer.
     */
    typename DataTraits::abstime_type abstime;

    /**
     * \brief Interval, in abstime units per index, of the fit sequence.
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

struct periodic_fit_result {
    double intercept;
    double slope;
    double mse; // mean squared error
};

class periodic_fitter {
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

    double n;
    double sigma_x;        // Sum of 0, 1, ..., n - 1
    double sigma_xx;       // Sum of 0^2, 1^2, ..., (n - 1)^2
    double det_XtX;        // Determinant of Xt X
    std::vector<double> x; // 0, ..., n - 1

  public:
    // n should be at least 3. If it is 2, mse will be NaN. If it is 0 or 1,
    // intercept and slope will be NaN.
    explicit periodic_fitter(std::uint64_t length)
        : n(static_cast<double>(length)), sigma_x((n - 1.0) * n * 0.5),
          sigma_xx((n - 1.0) * n * (2.0 * n - 1.0) / 6.0),
          det_XtX(n * sigma_xx - sigma_x * sigma_x), x(length) {
        std::iota(x.begin(), x.end(), 0.0);
    }

    auto fit(std::vector<double> const &y) const noexcept
        -> periodic_fit_result {
        assert(static_cast<double>(y.size()) == n);

        // Sum of y_0, y_1, ..., y_n-1
        double const sigma_y = std::reduce(y.cbegin(), y.cend());

        // Sum of x_0 * y_0, x_1 * y_1, ..., x_n-1 * y_n-1
        double const sigma_xy =
            std::transform_reduce(x.cbegin(), x.cend(), y.cbegin(), 0.0);

        // Solve ordinary linear least squares:
        // [a b]t = (Xt X)^-1 Xt y
        double const a = (sigma_xx * sigma_y - sigma_x * sigma_xy) / det_XtX;
        double const b = (n * sigma_xy - sigma_x * sigma_y) / det_XtX;

        // Sum of squared residuals
        double const ssr = std::transform_reduce(
            x.cbegin(), x.cend(), y.cbegin(), 0.0, std::plus<>(),
            [&](double x, double y) noexcept {
                auto const yfit = a + b * x;
                auto const r = y - yfit;
                return r * r;
            });
        double const mse = ssr / (n - 2.0);

        return {a, b, mse};
    }
};

template <typename DataTraits, typename Event, typename Downstream>
class fit_periodic_sequences {
    using abstime_type = typename DataTraits::abstime_type;

    std::size_t len; // At least 3

    // Record times relative to first event of the series, to prevent overflow
    // or loss of precision on large abstime values.
    abstime_type first_tick_time{};
    abstime_type tick_offset; // Offset relative tick times so as not to be
                              // near zero (avoid subnormal intercept)
    std::vector<double> relative_ticks; // First element will be tick_offset.

    // Colder data (only used when fitting).
    periodic_fitter fitter;
    abstime_type min_interval_cutoff;
    abstime_type max_interval_cutoff;
    double mse_cutoff;

    Downstream downstream;

    void fail(std::string const &message) {
        throw std::runtime_error("fit periodic sequences: " + message);
    }

    LIBTCSPC_NOINLINE void fit_and_emit(abstime_type last_event_time) {
        auto const result = fitter.fit(relative_ticks);
        if (result.mse > mse_cutoff)
            return fail("mean squared error exceeded cutoff");
        if (result.slope < double(min_interval_cutoff) ||
            result.slope > double(max_interval_cutoff))
            return fail("estimated time interval was not in expected range");

        auto const rounded_intercept = std::llround(result.intercept);

        if constexpr (std::is_unsigned_v<abstime_type>) {
            if ((rounded_intercept < 0 &&
                 static_cast<abstime_type>(-rounded_intercept) + tick_offset >
                     first_tick_time) ||
                (rounded_intercept >= 0 &&
                 static_cast<abstime_type>(rounded_intercept) +
                         first_tick_time <
                     tick_offset))
                return fail(
                    "estimated start time is negative but abstime_type is unsigned");
        }
        auto const abstime =
            rounded_intercept < 0
                ? first_tick_time -
                      static_cast<abstime_type>(-rounded_intercept) -
                      tick_offset
                : first_tick_time +
                      static_cast<abstime_type>(rounded_intercept) -
                      tick_offset;

        auto max_time_shift =
            max_interval_cutoff * static_cast<abstime_type>(len);
        if constexpr (std::is_unsigned_v<abstime_type>) {
            if (max_time_shift > last_event_time) {
                // The case of negative abstime is already checked above, so
                // effectively disable the max-time-shift check.
                max_time_shift = last_event_time;
            }
        }
        if (abstime < last_event_time - max_time_shift)
            return fail(
                "estimated start time was earlier than guaranteed time bound");

        downstream.handle(
            start_and_interval_event<DataTraits>{abstime, result.slope});
    }

  public:
    explicit fit_periodic_sequences(
        std::size_t length,
        std::array<typename DataTraits::abstime_type, 2> min_max_interval,
        double max_mse, Downstream &&downstream)
        : len(length), tick_offset(min_max_interval[1] + 10), fitter(length),
          min_interval_cutoff(min_max_interval[0]),
          max_interval_cutoff(min_max_interval[1]), mse_cutoff(max_mse),
          downstream(std::move(downstream)) {
        if (length < 3)
            throw std::invalid_argument(
                "fit_periodic_sequences length must be at least 3");
        if (min_interval_cutoff > max_interval_cutoff)
            throw std::invalid_argument(
                "fit_periodic_sequences min interval cutoff must be less than or equal to max interval cutoff");
        if (max_interval_cutoff <= 0)
            throw std::invalid_argument(
                "fit_periodic_sequences max interval cutoff must be positive");
        relative_ticks.reserve(length);
    }

    void handle(Event const &event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);

        if (relative_ticks.empty())
            first_tick_time = event.abstime;

        relative_ticks.push_back(static_cast<double>(
            event.abstime - first_tick_time + tick_offset));

        if (relative_ticks.size() == len) {
            fit_and_emit(event.abstime);
            relative_ticks.clear();
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that fits fixed-length periodic sequences of
 * events and estimates the start time and interval.
 *
 * \ingroup processors-timing
 *
 * The processor accepts a single event type, \c Event. Every \e length events
 * are grouped together and a model of regularly spaced events is fit to their
 * abstimes. If the fit is successful (see below for criteria), then a \c
 * start_and_interval_event is emitted, whose abstime is the estimated time of
 * the first event (rounded to integer) and which contains the estimated event
 * interval. If the fit is not successful, an error is generated.
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
auto fit_periodic_sequences(
    std::size_t length,
    std::array<typename DataTraits::abstime_type, 2> min_max_interval,
    double max_mse, Downstream &&downstream) {
    return internal::fit_periodic_sequences<DataTraits, Event, Downstream>(
        length, min_max_interval, max_mse,
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
