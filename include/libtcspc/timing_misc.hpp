/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "data_types.hpp"
#include "errors.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <cmath>
#include <cstddef>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

/**
 * \brief Event representing a summarized model of a periodic sequence of
 * events.
 *
 * \ingroup events-timing-modeling
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
struct periodic_sequence_model_event {
    /**
     * \brief Absolute time of this event, used as a reference point.
     */
    typename DataTypes::abstime_type abstime;

    /**
     * \brief The estimated time of the first event, relative to `abstime`.
     *
     * The modeled time of the first tick of the sequence is at _abstime_ +
     * _delay_.
     */
    double delay;

    /**
     * \brief Interval, in abstime units per index, of the modeled sequence.
     */
    double interval;

    /** \brief Equality comparison operator. */
    friend auto
    operator==(periodic_sequence_model_event const &lhs,
               periodic_sequence_model_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.delay == rhs.delay &&
               lhs.interval == rhs.interval;
    }

    /** \brief Inequality comparison operator. */
    friend auto
    operator!=(periodic_sequence_model_event const &lhs,
               periodic_sequence_model_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto
    operator<<(std::ostream &stream,
               periodic_sequence_model_event const &event) -> std::ostream & {
        return stream << "offset_and_interval(" << event.abstime << " + "
                      << event.delay << ", " << event.interval << ')';
    }
};

/**
 * \brief Event representing a prescription for one-shot timing generation with
 * real (fractional) delay.
 *
 * \ingroup events-timing-modeling
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
struct real_one_shot_timing_event {
    /**
     * \brief Absolute time of this event, used as a reference point.
     */
    typename DataTypes::abstime_type abstime;

    /**
     * \brief The time delay relative to `abstime`.
     */
    double delay;

    /** \brief Equality comparison operator. */
    friend auto
    operator==(real_one_shot_timing_event const &lhs,
               real_one_shot_timing_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.delay == rhs.delay;
    }

    /** \brief Inequality comparison operator. */
    friend auto
    operator!=(real_one_shot_timing_event const &lhs,
               real_one_shot_timing_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto
    operator<<(std::ostream &stream,
               real_one_shot_timing_event const &event) -> std::ostream & {
        return stream << "real_one_shot_timing(" << event.abstime << " + "
                      << event.delay << ')';
    }
};

/**
 * \brief Event representing a prescription for linear timing generation with
 * real (fractional) delay and interval.
 *
 * \ingroup events-timing-modeling
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
struct real_linear_timing_event {
    /**
     * \brief Absolute time of this event, used as a reference point.
     */
    typename DataTypes::abstime_type abstime;

    /**
     * \brief The time delay relative to `abstime`.
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
    friend auto
    operator==(real_linear_timing_event const &lhs,
               real_linear_timing_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.delay == rhs.delay &&
               lhs.interval == rhs.interval && lhs.count == rhs.count;
    }

    /** \brief Inequality comparison operator. */
    friend auto
    operator!=(real_linear_timing_event const &lhs,
               real_linear_timing_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto
    operator<<(std::ostream &stream,
               real_linear_timing_event const &event) -> std::ostream & {
        return stream << "real_linear_timing(" << event.abstime << " + "
                      << event.delay << ", " << event.interval << ", "
                      << event.count << ')';
    }
};

namespace internal {

template <typename DataTypes, typename Downstream>
class retime_periodic_sequences {
    static_assert(
        is_processor_v<Downstream, periodic_sequence_model_event<DataTypes>>);

    using abstime_type = typename DataTypes::abstime_type;

    abstime_type max_shift;

    Downstream downstream;

  public:
    explicit retime_periodic_sequences(
        arg::max_time_shift<typename DataTypes::abstime_type> max_time_shift,
        Downstream downstream)
        : max_shift(max_time_shift.value), downstream(std::move(downstream)) {
        if (max_shift < 0)
            throw std::invalid_argument(
                "retime_periodic_sequences max_time_shift must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "retime_periodic_sequences");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT>
    void handle(periodic_sequence_model_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type, abstime_type>);

        auto delta = std::floor(event.delay) - 1.0;
        if (std::abs(delta) > static_cast<double>(max_shift))
            throw data_validation_error(
                "retime periodic sequence: abstime would shift more than max time shift");

        abstime_type abstime{};
        if constexpr (std::is_unsigned_v<abstime_type>) {
            if (delta < 0.0) {
                auto ndelta = static_cast<abstime_type>(-delta);
                if (ndelta > event.abstime)
                    throw data_validation_error(
                        "retime periodic sequence: abstime would be negative but abstime_type is unsigned");
                abstime = event.abstime - ndelta;
            } else {
                abstime = event.abstime + static_cast<abstime_type>(delta);
            }
        } else {
            abstime = event.abstime + static_cast<abstime_type>(delta);
        }

        downstream.handle(periodic_sequence_model_event<DataTypes>{
            abstime, event.delay - delta, event.interval});
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that adjusts the abstime of
 * `tcspc::periodic_sequence_model_event` to be earlier than the modeled
 * sequence.
 *
 * \ingroup processors-timing-modeling
 *
 * Events of type `tcspc::periodic_sequence_model_event` (with matching
 * `abstime_type`) have their `abstime` and `delay` noramlized, such that
 * `delay` is at least 1.0 and no more than 2.0, without altering the modeled
 * tick sequence.
 *
 * This means that the events have an `abstime` before any of the modeled tick
 * times of the sequences they represent, so that they can be used for event
 * generation downstream.
 *
 * The choice of the `start_time` range of `[1.0, 2.0)` (rather than
 * `[0.0, 1.0)`) is to avoid subnormal floating point values.
 *
 * If the adjustment would result in altering the `abstime` by more than \p
 * max_time_shift (in either direction), processing is halted with an error.
 * This can be used to help make sure that the emitted events have a
 * monotonically increasing `abstime`.
 *
 * If the adjustment would result in a negative `abstime`, but `abstime_type`
 * is an unsigned integer type, processing is halted with an error.
 *
 * No other events are handled (because this processor would cause their
 * abstimes to be out of order).
 *
 * \attention The `abstime` of incoming events must be monotonically
 * non-decreasing and must not wrap around. The `abstime` of the result must
 * not overflow or underflow.
 *
 * \see `tcspc::fit_periodic_sequences()`
 *
 * \tparam DataTypes data type set specifying `abstime_type` and emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param max_time_shift maximum allowed (absolute value of) timeshift
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::periodic_sequence_model_event<DT>`: emit with normalized `abstime`
 *   and `delay` as `tcspc::periodic_sequence_model_event<DataTypes>`; throw
 *   `tcspc::data_validation_error` if the time shift or result range criteria
 *   are not met
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto retime_periodic_sequences(
    arg::max_time_shift<typename DataTypes::abstime_type> max_time_shift,
    Downstream &&downstream) {
    return internal::retime_periodic_sequences<DataTypes, Downstream>(
        max_time_shift, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename DataTypes, typename Downstream>
class extrapolate_periodic_sequences {
    static_assert(
        is_processor_v<Downstream, real_one_shot_timing_event<DataTypes>>);

    double m;
    Downstream downstream;

  public:
    explicit extrapolate_periodic_sequences(
        arg::tick_index<std::size_t> tick_index, Downstream downstream)
        : m(static_cast<double>(tick_index.value)),
          downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "extrapolate_periodic_sequences");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT>
    void handle(periodic_sequence_model_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        downstream.handle(real_one_shot_timing_event<DataTypes>{
            event.abstime, event.delay + event.interval * m});
    }

    template <typename DT>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(periodic_sequence_model_event<DT> &&event) {
        handle(static_cast<periodic_sequence_model_event<DT> const &>(event));
    }

    template <typename OtherEvent,
              typename = std::enable_if_t<
                  handles_event_v<Downstream, remove_cvref_t<OtherEvent>>>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that emits an extrapolated one-shot timing event
 * based on `tcspc::periodic_sequence_model_event`.
 *
 * \ingroup processors-timing-modeling
 *
 * Events of type `tcspc::periodic_sequence_model_event` (with matching
 * `abstime_type`) are converted to `tcspc::real_one_shot_timing_event` with
 * the same `abstime` and a `delay` computed by extrapolating the model
 * sequence to the given \p tick_index.
 *
 * All other events are passed through.
 *
 * \remark This is one way to synthesize an extra tick needed for use with
 * `tcspc::convert_sequences_to_start_stop()`.
 *
 * \attention The `abstime` of incoming events must be monotonically
 * non-decreasing and must not wrap around. The `abstime` of the result must
 * not overflow or underflow.
 *
 * \tparam DataTypes data type set specifying `abstime_type` and emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param tick_index tick index to extrapolate to
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::periodic_sequence_model_event<DT>`: emit
 *   `tcspc::real_one_shot_timing_event<DataTypes>` with the same `abstime`
 *   but the `delay` offset by `interval` times `tick_index`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto extrapolate_periodic_sequences(arg::tick_index<std::size_t> tick_index,
                                    Downstream &&downstream) {
    return internal::extrapolate_periodic_sequences<DataTypes, Downstream>(
        tick_index, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename DataTypes, typename Downstream>
class add_count_to_periodic_sequences {
    static_assert(
        is_processor_v<Downstream, real_linear_timing_event<DataTypes>>);

    std::size_t ct;
    Downstream downstream;

  public:
    explicit add_count_to_periodic_sequences(arg::count<std::size_t> count,
                                             Downstream downstream)
        : ct(count.value), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "add_count_to_periodic_sequences");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT>
    void handle(periodic_sequence_model_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        downstream.handle(real_linear_timing_event<DataTypes>{
            event.abstime, event.delay, event.interval, ct});
    }

    template <typename DT>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(periodic_sequence_model_event<DT> &&event) {
        handle(static_cast<periodic_sequence_model_event<DT> const &>(event));
    }

    template <typename OtherEvent,
              typename = std::enable_if_t<
                  handles_event_v<Downstream, remove_cvref_t<OtherEvent>>>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that emits a linear timing event based on
 * `tcspc::periodic_sequence_model_event` by adding a fixed sequence length.
 *
 * \ingroup processors-timing-modeling
 *
 * Events of type `tcspc::periodic_sequence_model_event` (with matching
 * `abstime_type`) are converted to `tcspc::real_linear_timing_event` with the
 * same `abstime`, `delay`, and `interval`, and with the given \p count.
 *
 * Typically, this processor is applied to the output of
 * `tcspc::retime_periodic_sequences()`.
 *
 * All other events are passed through.
 *
 * \tparam DataTypes data type set specifying `abstime_type` and emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param count sequence length to use
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::periodic_sequence_model_event<DT>`: emit
 *   `tcspc::real_linear_timing_event<DataTypes>` with the same `abstime`,
 *   `delay`, and `interval` and added `count`.
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto add_count_to_periodic_sequences(arg::count<std::size_t> count,
                                     Downstream &&downstream) {
    return internal::add_count_to_periodic_sequences<DataTypes, Downstream>(
        count, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename TickEvent, typename StartEvent, typename StopEvent,
          typename Downstream>
class convert_sequences_to_start_stop {
    static_assert(is_processor_v<Downstream, StartEvent, StopEvent>);

    static_assert(
        std::is_same_v<decltype(std::declval<TickEvent>().abstime),
                       decltype(std::declval<StartEvent>().abstime)>);
    static_assert(std::is_same_v<decltype(std::declval<TickEvent>().abstime),
                                 decltype(std::declval<StopEvent>().abstime)>);

    std::size_t input_len;
    std::size_t seen = 0;
    Downstream downstream;

  public:
    explicit convert_sequences_to_start_stop(arg::count<std::size_t> count,
                                             Downstream downstream)
        : input_len(count.value + 1), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "convert_sequences_to_start_stop");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void handle(TickEvent const &event) {
        if (seen > 0) {
            StopEvent e{};
            e.abstime = event.abstime;
            downstream.handle(std::move(e));
        }
        ++seen;
        if (seen < input_len) {
            StartEvent e{};
            e.abstime = event.abstime;
            downstream.handle(std::move(e));
        } else {
            seen = 0;
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(TickEvent &&event) {
        handle(static_cast<TickEvent const &>(event));
    }

    template <typename OtherEvent,
              typename = std::enable_if_t<
                  handles_event_v<Downstream, remove_cvref_t<OtherEvent>>>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that converts sequences of ticks to sequences of
 * start-stop event pairs with no gaps.
 *
 * \ingroup processors-timing-modeling
 *
 * Every sequence of `count + 1` events of type \p TickEvent is replaced by
 * a series of \p StartEvent and \p StopEvent events that bracket each tick
 * interval. The \p StopEvent for one interval and the \p StartEvent for the
 * next interval are emitted with the same abstime.
 *
 * This can be used to synthesize the start and stop events for
 * `tcspc::scan_histograms()`, given a single sequence of timing markers.
 * (Another method is to synthesize the stop events as a delayed copy of the
 * start events, using `tcspc::generate()` with
 * `tcspc::one_shot_timing_generator`.)
 *
 * \attention Beware of fencepost errors: `count` is the number of start-stop
 * pairs in each sequence, which is one less than the number of ticks needed to
 * produce them.
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
 *
 * \return processor
 *
 * \par Events handled
 * - `TickEvent`: emit `StopEvent` if not the first tick in a sequence, then
 *   `StartEvent` if not the last tick in a sequence, where a sequence is every
 *   series of `count + 1` ticks
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename TickEvent, typename StartEvent, typename StopEvent,
          typename Downstream>
auto convert_sequences_to_start_stop(arg::count<std::size_t> count,
                                     Downstream &&downstream) {
    return internal::convert_sequences_to_start_stop<TickEvent, StartEvent,
                                                     StopEvent, Downstream>(
        count, std::forward<Downstream>(downstream));
}

} // namespace tcspc
