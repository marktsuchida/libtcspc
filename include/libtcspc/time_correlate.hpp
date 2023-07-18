/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "time_tagged_events.hpp"

#include <cassert>
#include <cmath>
#include <exception>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// For now, we only support setting the emitted event's abstime to the start
// time, stop time, or midpoint. For CFD-like usage, it is potentially useful
// to split the time in arbitrary ratios other than 1:1; this could be
// supported, but probably should use a run-time floating point ratio, given
// that the ratio would be something experimentally determined and not a simple
// integer ratio.

template <typename DataTraits, bool UseStartTimeAndChannel,
          typename Downstream>
class time_correlate_at_start_or_stop {
    Downstream downstream;

  public:
    explicit time_correlate_at_start_or_stop(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    template <typename DT>
    void handle_event(detection_pair_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);
        auto const difftime = event.second.abstime - event.first.abstime;
        auto const &anchor =
            UseStartTimeAndChannel ? event.first : event.second;
        downstream.handle_event(time_correlated_detection_event<DataTraits>{
            {{anchor.abstime}, anchor.channel},
            static_cast<typename DataTraits::difftime_type>(difftime)});
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

template <typename DataTraits, bool UseStartChannel, typename Downstream>
class time_correlate_at_midpoint {
    Downstream downstream;

  public:
    explicit time_correlate_at_midpoint(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    template <typename DT>
    void handle_event(detection_pair_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);
        auto const difftime = event.second.abstime - event.first.abstime;
        auto const abstime = event.first.abstime + difftime / 2;
        auto const channel =
            UseStartChannel ? event.first.channel : event.second.channel;
        downstream.handle_event(time_correlated_detection_event<DataTraits>{
            {{abstime}, channel},
            static_cast<typename DataTraits::difftime_type>(difftime)});
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

template <typename DataTraits, bool UseStartChannel, typename Downstream>
class time_correlate_at_fraction {
    double fraction; // 0.0-1.0 for internal division of start-stop
    Downstream downstream;

  public:
    explicit time_correlate_at_fraction(double fraction,
                                        Downstream &&downstream)
        : fraction(fraction), downstream(std::move(downstream)) {}

    template <typename DT>
    void handle_event(detection_pair_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);
        auto const difftime = event.second.abstime - event.first.abstime;
        auto const abstime =
            event.first.abstime +
            static_cast<typename DataTraits::abstime_type>(
                std::llround(static_cast<double>(difftime) * fraction));
        auto const channel =
            UseStartChannel ? event.first.channel : event.second.channel;
        downstream.handle_event(time_correlated_detection_event<DataTraits>{
            {{abstime}, channel},
            static_cast<typename DataTraits::difftime_type>(difftime)});
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at the start time of the pair.
 *
 * \ingroup processors-timing
 *
 * No reordering of events takes place. If the incoming events have their stop
 * time in order and start time within a known time window of the stop time,
 * then the output events are time-bound out-of-order with that window size.
 *
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by both \c abstime_type and \c difftime_type without
 * overflowing.
 *
 * \see time_correlate_at_stop
 * \see time_correlate_at_midpoint
 * \see time_correlate_at_fraction
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto time_correlate_at_start(Downstream &&downstream) {
    return internal::time_correlate_at_start_or_stop<DataTraits, true,
                                                     Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at the stop time of the pair.
 *
 * \ingroup processors-timing
 *
 * No reordering of events takes place. The output events are in order if the
 * stop times of the incoming pairs are in order.
 *
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by both \c abstime_type and \c difftime_type without
 * overflowing.
 *
 * \see time_correlate_at_start
 * \see time_correlate_at_midpoint
 * \see time_correlate_at_fraction
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto time_correlate_at_stop(Downstream &&downstream) {
    return internal::time_correlate_at_start_or_stop<DataTraits, false,
                                                     Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at the midpoint between the start and stop
 * times of the pair.
 *
 * \ingroup processors-timing
 *
 * No reordering of events takes place. If the incoming events have their stop
 * time in order and start time within a known time window of the stop time,
 * then the output events are time-bound out-of-order with half of that window
 * size.
 *
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by both \c abstime_type and \c difftime_type without
 * overflowing.
 *
 * \see time_correlate_at_start
 * \see time_correlate_at_stop
 * \see time_correlate_at_fraction
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type
 *
 * \tparam UseStartChannel if true, use the channel of the start of the pair as
 * the channel of the emitted events; otherwise use the stop channel
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits,
          bool UseStartChannel = false, typename Downstream>
auto time_correlate_at_midpoint(Downstream &&downstream) {
    return internal::time_correlate_at_midpoint<DataTraits, UseStartChannel,
                                                Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at a fractional dividing point between the
 * start and stop times of the pair.
 *
 * \ingroup processors-timing
 *
 * No reordering of events takes place. If the incoming events have their stop
 * time in order and start time within a known time window of the stop time,
 * then the output events are time-bound out-of-order with (1 - fraction) times
 * that window size.
 *
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by \c abstime_type, \c difftime_type, and \c double
 * without overflowing.
 *
 * \see time_correlate_at_start
 * \see time_correlate_at_stop
 * \see time_correlate_at_midpoint
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type
 *
 * \tparam UseStartChannel if true, use the channel of the start of the pair as
 * the channel of the emitted events; otherwise use the stop channel
 *
 * \tparam Downstream downstream processor type
 *
 * \param fraction the dividing fraction of start and stop time: 0.0 for start
 * time; 1.0 for stop time; 0.5 for the midpoint
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits,
          bool UseStartChannel = false, typename Downstream>
auto time_correlate_at_fraction(double fraction, Downstream &&downstream) {
    return internal::time_correlate_at_fraction<DataTraits, UseStartChannel,
                                                Downstream>(
        fraction, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename Downstream> class negate_difftime {
    Downstream downstream;

  public:
    explicit negate_difftime(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    template <typename DataTraits>
    void handle_event(
        time_correlated_detection_event<DataTraits> const &event) noexcept {
        static_assert(
            std::is_signed_v<typename DataTraits::difftime_type>,
            "difftime_type of time_correlated_detection_event used with negate_difftime must be a signed integer type");
        time_correlated_detection_event<DataTraits> copy(event);
        copy.difftime = -event.difftime;
        downstream.handle_event(copy);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

template <typename DataTraits, typename Downstream>
class remove_time_correlation {
    Downstream downstream;

  public:
    explicit remove_time_correlation(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    template <typename DT>
    void
    handle_event(time_correlated_detection_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);

        downstream.handle_event(
            detection_event<DataTraits>{{{event.abstime}, event.channel}});
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that changes the sign of difftime in
 * time-correlated detection events.
 *
 * \ingroup processors-timing
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename Downstream> auto negate_difftime(Downstream &&downstream) {
    return internal::negate_difftime<Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that removes the difftime from detection events.
 *
 * \ingroup processors-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type.
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto remove_time_correlation(Downstream &&downstream) {
    return internal::remove_time_correlation<DataTraits, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
