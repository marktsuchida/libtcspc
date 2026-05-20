/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "int_arith.hpp"
#include "introspect.hpp"
#include "numeric_traits.hpp"
#include "processor.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cmath>
#include <stdexcept>
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

template <typename NumericTraits, bool UseStartTimeAndChannel,
          typename Downstream>
class time_correlate_at_start_or_stop {
    static_assert(
        processor<Downstream, time_correlated_detection_event<NumericTraits>>);

    Downstream downstream;

  public:
    explicit time_correlate_at_start_or_stop(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "time_correlate_at_start_or_stop");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename NT>
    void handle(std::array<detection_event<NT>, 2> const &event) {
        static_assert(std::is_same_v<typename NT::abstime_type,
                                     typename NumericTraits::abstime_type>);
        static_assert(std::is_same_v<typename NT::channel_type,
                                     typename NumericTraits::channel_type>);
        auto const &anchor = UseStartTimeAndChannel ? event[0] : event[1];
        auto const difftime =
            convert_with_check<typename NumericTraits::difftime_type>(
                subtract_with_check(event[1].abstime, event[0].abstime));
        downstream.handle(time_correlated_detection_event<NumericTraits>{
            anchor.abstime, anchor.channel, difftime});
    }

    template <typename NT>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(std::array<detection_event<NT>, 2> &&event) {
        handle(static_cast<std::array<detection_event<NT>, 2> const &>(event));
    }

    template <typename OtherEvent>
        requires handler_for<Downstream, std::remove_cvref_t<OtherEvent>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

template <typename NumericTraits, bool UseStartChannel, typename Downstream>
class time_correlate_at_midpoint {
    static_assert(
        processor<Downstream, time_correlated_detection_event<NumericTraits>>);

    Downstream downstream;

  public:
    explicit time_correlate_at_midpoint(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "time_correlate_at_midpoint");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename NT>
    void handle(std::array<detection_event<NT>, 2> const &event) {
        static_assert(std::is_same_v<typename NT::abstime_type,
                                     typename NumericTraits::abstime_type>);
        static_assert(std::is_same_v<typename NT::channel_type,
                                     typename NumericTraits::channel_type>);
        auto const difftime =
            subtract_with_check(event[1].abstime, event[0].abstime);
        auto const abstime = event[0].abstime + difftime / 2;
        auto const channel =
            UseStartChannel ? event[0].channel : event[1].channel;
        downstream.handle(time_correlated_detection_event<NumericTraits>{
            abstime, channel,
            convert_with_check<typename NumericTraits::difftime_type>(
                difftime)});
    }

    template <typename NT>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(std::array<detection_event<NT>, 2> &&event) {
        handle(static_cast<std::array<detection_event<NT>, 2> const &>(event));
    }

    template <typename OtherEvent>
        requires handler_for<Downstream, std::remove_cvref_t<OtherEvent>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

template <typename NumericTraits, bool UseStartChannel, typename Downstream>
class time_correlate_at_fraction {
    static_assert(
        processor<Downstream, time_correlated_detection_event<NumericTraits>>);

    double frac; // 0.0-1.0 for internal division of start-stop
    Downstream downstream;

  public:
    explicit time_correlate_at_fraction(arg::fraction<double> fraction,
                                        Downstream downstream)
        : frac(fraction.value), downstream(std::move(downstream)) {
        if (frac < 0.0 || frac > 1.0)
            throw std::invalid_argument(
                "time_correlate_at_fraction fraction must be in range [0.0, 1.0]");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "time_correlate_at_fraction");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename NT>
    void handle(std::array<detection_event<NT>, 2> const &event) {
        static_assert(std::is_same_v<typename NT::abstime_type,
                                     typename NumericTraits::abstime_type>);
        static_assert(std::is_same_v<typename NT::channel_type,
                                     typename NumericTraits::channel_type>);
        auto const difftime =
            subtract_with_check(event[1].abstime, event[0].abstime);
        auto const abstime =
            event[0].abstime +
            static_cast<NumericTraits::abstime_type>(
                std::llround(static_cast<double>(difftime) * frac));
        auto const channel =
            UseStartChannel ? event[0].channel : event[1].channel;
        downstream.handle(time_correlated_detection_event<NumericTraits>{
            abstime, channel,
            convert_with_check<typename NumericTraits::difftime_type>(
                difftime)});
    }

    template <typename NT>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(std::array<detection_event<NT>, 2> &&event) {
        handle(static_cast<std::array<detection_event<NT>, 2> const &>(event));
    }

    template <typename OtherEvent>
        requires handler_for<Downstream, std::remove_cvref_t<OtherEvent>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at the start time of the pair.
 *
 * \ingroup processors-time-corr
 *
 * No reordering of events takes place. If the incoming events have their stop
 * time in order and start time within a known time window of the stop time,
 * then the output events are time-bound out-of-order with that window size.
 *
 * \attention The difference between the abstime of the start and stop event in
 * each pair must be representable by both `abstime_type` and `difftime_type`
 * without overflowing.
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`,
 * `channel_type`, and `difftime_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::std::array<detection_event<NT>, 2>`: emit
 *   `tcspc::time_correlated_detection_event<NumericTraits>` with
 *   - `abstime` set equal to that of the first event of the pair
 *   - `channel` set to the channel of the first event of the pair
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename NumericTraits = default_numeric_traits, typename Downstream>
auto time_correlate_at_start(Downstream downstream) {
    return internal::time_correlate_at_start_or_stop<NumericTraits, true,
                                                     Downstream>(
        std::move(downstream));
}

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at the stop time of the pair.
 *
 * \ingroup processors-time-corr
 *
 * No reordering of events takes place. The output events are in order if the
 * stop times of the incoming pairs are in order.
 *
 * \attention The difference between the abstime of the start and stop event in
 * each pair must be representable by both `abstime_type` and `difftime_type`
 * without overflowing.
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`,
 * `channel_type`, and `difftime_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::std::array<detection_event<NT>, 2>`: emit
 *   `tcspc::time_correlated_detection_event<NumericTraits>` with
 *   - `abstime` set equal to that of the second event of the pair
 *   - `channel` set to the channel of the second event of the pair
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename NumericTraits = default_numeric_traits, typename Downstream>
auto time_correlate_at_stop(Downstream downstream) {
    return internal::time_correlate_at_start_or_stop<NumericTraits, false,
                                                     Downstream>(
        std::move(downstream));
}

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at the midpoint between the start and stop
 * times of the pair.
 *
 * \ingroup processors-time-corr
 *
 * No reordering of events takes place. If the incoming events have their stop
 * time in order and start time within a known time window of the stop time,
 * then the output events are time-bound out-of-order with half of that window
 * size.
 *
 * \attention The difference between the abstime of the start and stop event in
 * each pair must be representable by both `abstime_type` and `difftime_type`
 * without overflowing.
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`,
 * `channel_type`, and `difftime_type`
 *
 * \tparam UseStartChannel if true, use the channel of the start of the pair as
 * the channel of the emitted events; otherwise use the stop channel
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::std::array<detection_event<NT>, 2>`: emit
 *   `tcspc::time_correlated_detection_event<NumericTraits>` with
 *   - `abstime` set to the midpoint of the pair's abstimes
 *   - `channel` set to the channel of the first event of the pair if
 *     `UseStartChannel` is true, else of the second
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename NumericTraits = default_numeric_traits,
          bool UseStartChannel = false, typename Downstream>
auto time_correlate_at_midpoint(Downstream downstream) {
    return internal::time_correlate_at_midpoint<NumericTraits, UseStartChannel,
                                                Downstream>(
        std::move(downstream));
}

/**
 * \brief Create a processor that collapses detection pairs into
 * time-correlated detection events at a fractional dividing point between the
 * start and stop times of the pair.
 *
 * \ingroup processors-time-corr
 *
 * No reordering of events takes place. If the incoming events have their stop
 * time in order and start time within a known time window of the stop time,
 * then the output events are time-bound out-of-order with `1 - fraction` times
 * that window size.
 *
 * \attention The difference between the abstime of the start and stop event in
 * each pair must be representable by `abstime_type`, `difftime_type`, and
 * `double` without overflowing.
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`,
 * `channel_type`, and `difftime_type`
 *
 * \tparam UseStartChannel if true, use the channel of the start of the pair as
 * the channel of the emitted events; otherwise use the stop channel
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param fraction the dividing fraction of start and stop time: 0.0 for start
 * time; 1.0 for stop time; 0.5 for the midpoint
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::std::array<detection_event<NT>, 2>`: emit
 *   `tcspc::time_correlated_detection_event<NumericTraits>` with
 *   - `abstime` set to the fractional division point of the pair's abstimes
 *   - `channel` set to the channel of the first event of the pair if
 *     `UseStartChannel` is true, else of the second
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename NumericTraits = default_numeric_traits,
          bool UseStartChannel = false, typename Downstream>
auto time_correlate_at_fraction(arg::fraction<double> fraction,
                                Downstream downstream) {
    return internal::time_correlate_at_fraction<NumericTraits, UseStartChannel,
                                                Downstream>(
        fraction, std::move(downstream));
}

namespace internal {

template <typename NumericTraits, typename Downstream>
    requires processor<Downstream, detection_event<NumericTraits>>
class remove_time_correlation {
    Downstream downstream;

  public:
    explicit remove_time_correlation(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "remove_time_correlation");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename NT>
    void handle(time_correlated_detection_event<NT> const &event) {
        static_assert(std::is_same_v<typename NT::abstime_type,
                                     typename NumericTraits::abstime_type>);
        static_assert(std::is_same_v<typename NT::channel_type,
                                     typename NumericTraits::channel_type>);

        downstream.handle(
            detection_event<NumericTraits>{event.abstime, event.channel});
    }

    template <typename NT>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(time_correlated_detection_event<NT> &&event) {
        handle(
            static_cast<time_correlated_detection_event<NT> const &>(event));
    }

    template <typename OtherEvent>
        requires handler_for<Downstream, std::remove_cvref_t<OtherEvent>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that removes the difftime from detection events.
 *
 * \ingroup processors-time-corr
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type` and
 * `channel_type`
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::time_correlated_detection_event<NT>`: emit
 *   `tcspc::detection_event<NumericTraits>`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename NumericTraits = default_numeric_traits, typename Downstream>
auto remove_time_correlation(Downstream downstream) {
    return internal::remove_time_correlation<NumericTraits, Downstream>(
        std::move(downstream));
}

} // namespace tcspc
