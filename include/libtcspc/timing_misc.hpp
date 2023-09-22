/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <cmath>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

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
     * delay</tt>.
     */
    double delay;

    /**
     * \brief Interval, in abstime units per index, of the modeled sequence.
     */
    double interval;

    /** \brief Equality comparison operator. */
    friend auto operator==(periodic_sequence_event const &lhs,
                           periodic_sequence_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime && lhs.delay == rhs.delay &&
               lhs.interval == rhs.interval;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(periodic_sequence_event const &lhs,
                           periodic_sequence_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           periodic_sequence_event const &event)
        -> std::ostream & {
        return stream << "offset_and_interval(" << event.abstime << " + "
                      << event.delay << ", " << event.interval << ')';
    }
};

/**
 * \brief Event representing a prescription for one-shot timing generation with
 * real (fractional) delay.
 *
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct real_one_shot_timing_event {
    /**
     * \brief Absolute time of this event, used as a reference point.
     */
    typename DataTraits::abstime_type abstime;

    /**
     * \brief The time delay relative to \c abstime.
     */
    double delay;

    /** \brief Equality comparison operator. */
    friend auto operator==(real_one_shot_timing_event const &lhs,
                           real_one_shot_timing_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime && lhs.delay == rhs.delay;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(real_one_shot_timing_event const &lhs,
                           real_one_shot_timing_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           real_one_shot_timing_event const &event)
        -> std::ostream & {
        return stream << "real_one_shot_timing(" << event.abstime << " + "
                      << event.delay << ')';
    }
};

/**
 * \brief Event representing a prescription for linear timing generation with
 * real (fractional) delay and interval.
 *
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct real_linear_timing_event {
    /**
     * \brief Absolute time of this event, used as a reference point.
     */
    typename DataTraits::abstime_type abstime;

    /**
     * \brief The time delay relative to \c abstime.
     */
    double delay;

    /**
     * \brief Interval between the events in the represented sequence.
     */
    double interval;

    /**
     * \brief Number of events in the represented sequence.
     */
    std::size_t count;

    /** \brief Equality comparison operator. */
    friend auto operator==(real_linear_timing_event const &lhs,
                           real_linear_timing_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime && lhs.delay == rhs.delay &&
               lhs.interval == rhs.interval && lhs.count == rhs.count;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(real_linear_timing_event const &lhs,
                           real_linear_timing_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           real_linear_timing_event const &event)
        -> std::ostream & {
        return stream << "real_linear_timing(" << event.abstime << " + "
                      << event.delay << ", " << event.interval << ", "
                      << event.count << ')';
    }
};

namespace internal {

template <typename DataTraits, typename Downstream>
class retime_periodic_sequences {
    using abstime_type = typename DataTraits::abstime_type;

    abstime_type max_shift;

    Downstream downstream;

  public:
    explicit retime_periodic_sequences(
        typename DataTraits::abstime_type max_time_shift,
        Downstream downstream)
        : max_shift(max_time_shift), downstream(std::move(downstream)) {
        if (max_shift < 0)
            throw std::invalid_argument(
                "retime_periodic_sequences max_time_shift must not be negative");
    }

    template <typename DT>
    void handle(periodic_sequence_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type, abstime_type>);

        auto delta = std::floor(event.delay) - 1.0;
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
            abstime, event.delay - delta, event.interval});
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that adjusts the abstime of \c
 * periodic_sequence_event to be earlier than the modeled sequence.
 *
 * \ingroup processors-timing
 *
 * Events of type \c periodic_sequence_event (with matching \c abstime_type)
 * have their \c abstime and \c delay modified, such that \c delay is at least
 * 1.0 and no more than 2.0.
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
auto retime_periodic_sequences(
    typename DataTraits::abstime_type max_time_shift,
    Downstream &&downstream) {
    return internal::retime_periodic_sequences<DataTraits, Downstream>(
        max_time_shift, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename DataTraits, typename Downstream>
class extrapolate_periodic_sequences {
    double m;
    Downstream downstream;

  public:
    explicit extrapolate_periodic_sequences(std::size_t tick_index,
                                            Downstream downstream)
        : m(static_cast<double>(tick_index)),
          downstream(std::move(downstream)) {}

    template <typename DT>
    void handle(periodic_sequence_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        downstream.handle(real_one_shot_timing_event<DataTraits>{
            event.abstime, event.delay + event.interval * m});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that emits an extrapolated one-shot timing event
 * based on \c periodic_sequence_event.
 *
 * \ingroup processors-timing
 *
 * Events of type \c periodic_sequence_event (with matching \c abstime_type)
 * are converted to \c real_one_shot_timing_event with the same \c abstime and
 * a \c delay computed by extrapolated the model sequence to the given \e
 * tick_index.
 *
 * All other events are passed through.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and traits for
 * emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param tick_index tick index to extrapolate to
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto extrapolate_periodic_sequences(std::size_t tick_index,
                                    Downstream &&downstream) {
    return internal::extrapolate_periodic_sequences<DataTraits, Downstream>(
        tick_index, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename DataTraits, typename Downstream>
class add_count_to_periodic_sequences {
    std::size_t ct;
    Downstream downstream;

  public:
    explicit add_count_to_periodic_sequences(std::size_t count,
                                             Downstream downstream)
        : ct(count), downstream(std::move(downstream)) {}

    template <typename DT>
    void handle(periodic_sequence_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        downstream.handle(real_linear_timing_event<DataTraits>{
            event.abstime, event.delay, event.interval, ct});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that emits a linear timing event based on \c
 * periodic_sequence_event by adding a fixed sequence length.
 *
 * \ingroup processors-timing
 *
 * Events of type \c periodic_sequence_event (with matching \c abstime_type)
 * are converted to \c real_linear_timing_event with the same \c abstime, \c
 * delay, and \c interval, and with the given \e count.
 *
 * Typically, this processor should be applied to the output of \ref
 * retime_periodic_sequences.
 *
 * All other events are passed through.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and traits for
 * emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param count sequence length to use
 *
 * \param downstream downstream processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto add_count_to_periodic_sequences(std::size_t count,
                                     Downstream &&downstream) {
    return internal::add_count_to_periodic_sequences<DataTraits, Downstream>(
        count, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename TickEvent, typename StartEvent, typename StopEvent,
          typename Downstream>
class convert_sequences_to_start_stop {
    static_assert(
        std::is_same_v<decltype(std::declval<TickEvent>().abstime),
                       decltype(std::declval<StartEvent>().abstime)>);
    static_assert(std::is_same_v<decltype(std::declval<TickEvent>().abstime),
                                 decltype(std::declval<StopEvent>().abstime)>);

    std::size_t input_len;
    std::size_t seen = 0;
    Downstream downstream;

  public:
    explicit convert_sequences_to_start_stop(std::size_t count,
                                             Downstream downstream)
        : input_len(count + 1), downstream(std::move(downstream)) {}

    void handle(TickEvent const &event) {
        if (seen > 0) {
            StopEvent e{};
            e.abstime = event.abstime;
            downstream.handle(e);
        }
        ++seen;
        if (seen < input_len) {
            StartEvent e{};
            e.abstime = event.abstime;
            downstream.handle(e);
        } else {
            seen = 0;
        }
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that converts sequences of ticks to sequences of
 * start-stop event pairs with no gaps.
 *
 * \ingroup processors-timing
 *
 * Every sequence of \e count + 1 events of type \c TickEvent is replaced by
 * a series of \c StartEvent and \c StopEvent events that bracket each tick
 * interval. The \c StopEvent for one interval and the \c StartEvent for the
 * next interval are emitted at the same abstime.
 *
 * This can be used to synthesize the start and stop events for \ref
 * histogram_elementwise, given a single sequence of timing markers. (Another
 * method is to synthesize the stop events as a delayed copy of the start
 * events, using \ref generate with \ref one_shot_timing_generator.)
 *
 * All other events are passed through.
 *
 * \tparam TickEvent tick event type
 *
 * \tparam StartEvent start event type to emit
 *
 * \tparam StopEvent stop event type to emit
 *
 * \param count sequence length of the emitted start-stop pairs
 *
 * \param downstream doownstream processor
 */
template <typename TickEvent, typename StartEvent, typename StopEvent,
          typename Downstream>
auto convert_sequences_to_start_stop(std::size_t count,
                                     Downstream &&downstream) {
    return internal::convert_sequences_to_start_stop<TickEvent, StartEvent,
                                                     StopEvent, Downstream>(
        count, std::forward<Downstream>(downstream));
}

} // namespace tcspc
