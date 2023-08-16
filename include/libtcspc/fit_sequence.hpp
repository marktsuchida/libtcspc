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
 * \brief Event representing a summarized model of a periodic sequence of
 * events.
 *
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct periodic_sequence_event {
    /**
     * \brief Absolute time of this event, used as a reference point.
     */
    typename DataTraits::abstime_type abstime;

    /**
     * \brief The estimated time of the first event, relative to \c abstime.
     *
     * The modeled time of the first tick of the sequence is at <tt>abstime +
     * start_offset</tt>.
     */
    double start_offset;

    /**
     * \brief Interval, in abstime units per index, of the modeled sequence.
     */
    double interval;

    /** \brief Equality comparison operator. */
    friend auto operator==(periodic_sequence_event const &lhs,
                           periodic_sequence_event const &rhs) noexcept {
        return lhs.abstime == rhs.abstime &&
               lhs.start_offset == rhs.start_offset &&
               lhs.interval == rhs.interval;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(periodic_sequence_event const &lhs,
                           periodic_sequence_event const &rhs) noexcept {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           periodic_sequence_event const &event)
        -> std::ostream & {
        return stream << "offset_and_interval(" << event.abstime << " + "
                      << event.start_offset << ", " << event.interval << ')';
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

    [[nodiscard]] auto fit(std::vector<double> const &y) const noexcept
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
    double min_interval_cutoff;
    double max_interval_cutoff;
    double mse_cutoff;

    Downstream downstream;

    LIBTCSPC_NOINLINE void fit_and_emit(abstime_type last_tick_time) {
        auto const result = fitter.fit(relative_ticks);
        if (result.mse > mse_cutoff)
            throw std::runtime_error(
                "fit periodic sequences: mean squared error exceeded cutoff");
        if (result.slope < min_interval_cutoff ||
            result.slope > max_interval_cutoff)
            throw std::runtime_error(
                "fit periodic sequences: stimated time interval was not in expected range");

        // Convert intercept (relative to first_tick_time + tick_offset) to
        // start offset (relative to last_tick_time).
        auto const start_offset =
            result.intercept -
            static_cast<double>(last_tick_time - first_tick_time) -
            static_cast<double>(tick_offset);

        downstream.handle(periodic_sequence_event<DataTraits>{
            last_tick_time, start_offset, result.slope});
    }

  public:
    explicit fit_periodic_sequences(std::size_t length,
                                    std::array<double, 2> min_max_interval,
                                    double max_mse, Downstream &&downstream)
        : len(length),
          tick_offset(static_cast<abstime_type>(min_max_interval[1]) + 10),
          fitter(length), min_interval_cutoff(min_max_interval[0]),
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

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

template <typename DataTraits, typename Downstream>
class retime_periodic_sequence_events {
    using abstime_type = typename DataTraits::abstime_type;

    abstime_type max_shift;

    Downstream downstream;

  public:
    explicit retime_periodic_sequence_events(
        typename DataTraits::abstime_type max_time_shift,
        Downstream &&downstream)
        : max_shift(max_time_shift), downstream(std::move(downstream)) {
        if (max_shift < 0)
            throw std::invalid_argument(
                "retime_periodic_sequence_events max_time_shift must not be negative");
    }

    template <typename DT>
    void handle(periodic_sequence_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type, abstime_type>);

        auto delta = std::floor(event.start_offset) - 1.0;
        if (std::abs(delta) > static_cast<double>(max_shift))
            throw std::runtime_error(
                "retime periodic sequence: abstime would shift more than max time shift");

        abstime_type abstime{};
        if constexpr (std::is_unsigned_v<abstime_type>) {
            if (delta < 0.0) {
                auto ndelta = static_cast<abstime_type>(-delta);
                if (ndelta > event.abstime)
                    throw std::runtime_error(
                        "retime periodic sequence: abstime would be negative but abstime_type is unsigned");
                abstime = event.abstime - ndelta;
            } else {
                abstime = event.abstime + static_cast<abstime_type>(delta);
            }
        } else {
            abstime = event.abstime + static_cast<abstime_type>(delta);
        }

        downstream.handle(periodic_sequence_event<DataTraits>{
            abstime, event.start_offset - delta, event.interval});
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
 * abstimes. If the fit is successful (see below for criteria), then an \c
 * periodic_sequence_event is emitted, containing the fit results, upon
 * receiving the last \c Event of the series. If the fit is not successful,
 * processing is halted with an error.
 *
 * The emitted event's abstime is set to the abstime of the last observed \c
 * Event (during which handling the event is emitted). The emitted fit
 * parameters consist of a start offset and interval. The start offset is
 * relative to the emitted event's abstime.
 *
 * The fit is considered successful if all of the following criteria are
 * satisfied:
 *
 * -# the mean squared error is no more than \e max_mse
 * -# the estimated event interval is within \e min_max_interval
 *
 * This processor does not pass through \c Event, but passes through any other
 * event.
 *
 * \tparam DataTraits traits type specifying data types for emitted event
 *
 * \tparam Event event whose timing is to be fit
 *
 * \tparam Downstream downstream processor type
 *
 * \param length number of \c Event events to fit
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
auto fit_periodic_sequences(std::size_t length,
                            std::array<double, 2> min_max_interval,
                            double max_mse, Downstream &&downstream) {
    return internal::fit_periodic_sequences<DataTraits, Event, Downstream>(
        length, min_max_interval, max_mse,
        std::forward<Downstream>(downstream));
}

/**
 * Create a processor that adjusts the abstime of \c periodic_sequence_event to
 * be earlier than the modeled sequence.
 *
 * \ingroup processors-timing
 *
 * Events of type \c periodic_sequence_event (with matching \c abstime_type)
 * have their \c abstime and \c start_offset modified, such that \c
 * start_offset is at least 1.0 and no more than 2.0.
 *
 * This means that the events are timed before any of the modeled tick times of
 * the sequences they represent, so that they can be used for event generation
 * downstream.
 *
 * The choice of the \c start_time range of <tt>[1.0, 2.0)</tt> (rather than
 * <tt>[0.0, 1.0)</tt>) is to avoid subnormal floating point values.
 *
 * If the adjustment would result in altering the abstime by more than \e
 * max_time_shift (in either direction), processing is halted with an error.
 * This can be used to guarantee that any downstream (\ref merge) works
 * correctly (provided that the resulting abstimes are in order, which needs to
 * be guaranteed by the manner in which the original events were generated).
 *
 * If the adjustment would result in a negative abstime, but the abstime type
 * is an unsigned integer type, processing is halted with an error.
 *
 * No other events are handled (because this processor would cause their
 * abstimes to be out of order).
 *
 * \see fit_periodic_sequences
 *
 * \tparam DataTraits traits type specifying \c abstime_type and traits for
 * emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param max_time_shift maximum allowed (absolute value of) timeshift
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto retime_periodic_sequence_events(
    typename DataTraits::abstime_type max_time_shift,
    Downstream &&downstream) {
    return internal::retime_periodic_sequence_events<DataTraits, Downstream>(
        max_time_shift, std::forward<Downstream>(downstream));
}

} // namespace tcspc
