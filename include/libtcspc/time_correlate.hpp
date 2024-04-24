/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "time_tagged_events.hpp"

#include <cmath>
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

template <typename DataTypes, bool UseStartTimeAndChannel, typename Downstream>
class time_correlate_at_start_or_stop {
    Downstream downstream;

  public:
    explicit time_correlate_at_start_or_stop(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "time_correlate_at_start_or_stop");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(detection_pair_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);
        auto const difftime = event.second.abstime - event.first.abstime;
        auto const &anchor =
            UseStartTimeAndChannel ? event.first : event.second;
        downstream.handle(time_correlated_detection_event<DataTypes>{
            anchor.abstime, anchor.channel,
            static_cast<typename DataTypes::difftime_type>(difftime)});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

template <typename DataTypes, bool UseStartChannel, typename Downstream>
class time_correlate_at_midpoint {
    Downstream downstream;

  public:
    explicit time_correlate_at_midpoint(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "time_correlate_at_midpoint");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(detection_pair_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);
        auto const difftime = event.second.abstime - event.first.abstime;
        auto const abstime = event.first.abstime + difftime / 2;
        auto const channel =
            UseStartChannel ? event.first.channel : event.second.channel;
        downstream.handle(time_correlated_detection_event<DataTypes>{
            abstime, channel,
            static_cast<typename DataTypes::difftime_type>(difftime)});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

template <typename DataTypes, bool UseStartChannel, typename Downstream>
class time_correlate_at_fraction {
    double fraction; // 0.0-1.0 for internal division of start-stop
    Downstream downstream;

  public:
    explicit time_correlate_at_fraction(double fraction, Downstream downstream)
        : fraction(fraction), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "time_correlate_at_fraction");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(detection_pair_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);
        auto const difftime = event.second.abstime - event.first.abstime;
        auto const abstime =
            event.first.abstime +
            static_cast<typename DataTypes::abstime_type>(
                std::llround(static_cast<double>(difftime) * fraction));
        auto const channel =
            UseStartChannel ? event.first.channel : event.second.channel;
        downstream.handle(time_correlated_detection_event<DataTypes>{
            abstime, channel,
            static_cast<typename DataTypes::difftime_type>(difftime)});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
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
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by both `abstime_type` and `difftime_type` without
 * overflowing.
 *
 * \tparam DataTypes data type set specifying `abstime_type`, `channel_type`,
 * and `difftime_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::detection_pair_event<DT>`: emit
 *   `tcspc::time_correlated_detection_event<DataTypes>` with
 *   - `abstime` set equal to that of the first event of the pair
 *   - `channel` set to the channel of the first event of the pair if
 *     `UseStartChannel` is true, else of the second
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto time_correlate_at_start(Downstream &&downstream) {
    return internal::time_correlate_at_start_or_stop<DataTypes, true,
                                                     Downstream>(
        std::forward<Downstream>(downstream));
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
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by both `abstime_type` and `difftime_type` without
 * overflowing.
 *
 * \tparam DataTypes data type set specifying `abstime_type`, `channel_type`,
 * and `difftime_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::detection_pair_event<DT>`: emit
 *   `tcspc::time_correlated_detection_event<DataTypes>` with
 *   - `abstime` set equal to that of the second event of the pair
 *   - `channel` set to the channel of the first event of the pair if
 *     `UseStartChannel` is true, else of the second
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto time_correlate_at_stop(Downstream &&downstream) {
    return internal::time_correlate_at_start_or_stop<DataTypes, false,
                                                     Downstream>(
        std::forward<Downstream>(downstream));
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
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by both `abstime_type` and `difftime_type` without
 * overflowing.
 *
 * \tparam DataTypes data type set specifying `abstime_type`, `channel_type`,
 * and `difftime_type`
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
 * - `tcspc::detection_pair_event<DT>`: emit
 *   `tcspc::time_correlated_detection_event<DataTypes>` with
 *   - `abstime` set to the midpoint of the pair's abstimes
 *   - `channel` set to the channel of the first event of the pair if
 *     `UseStartChannel` is true, else of the second
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types,
          bool UseStartChannel = false, typename Downstream>
auto time_correlate_at_midpoint(Downstream &&downstream) {
    return internal::time_correlate_at_midpoint<DataTypes, UseStartChannel,
                                                Downstream>(
        std::forward<Downstream>(downstream));
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
 * The difference between the abstime of the start and stop event in each pair
 * must be representable by `abstime_type`, `difftime_type`, and `double`
 * without overflowing.
 *
 * \tparam DataTypes data type set specifying `abstime_type`, `channel_type`,
 * and `difftime_type`
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
 * - `tcspc::detection_pair_event<DT>`: emit
 *   `tcspc::time_correlated_detection_event<DataTypes>` with
 *   - `abstime` set to the fractional division point of the pair's abstimes
 *   - `channel` set to the channel of the first event of the pair if
 *     `UseStartChannel` is true, else of the second
 *   - `difftime` set to the `abstime` difference of the pair
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types,
          bool UseStartChannel = false, typename Downstream>
auto time_correlate_at_fraction(double fraction, Downstream &&downstream) {
    return internal::time_correlate_at_fraction<DataTypes, UseStartChannel,
                                                Downstream>(
        fraction, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename Downstream> class negate_difftime {
    Downstream downstream;

  public:
    explicit negate_difftime(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "negate_difftime");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DataTypes>
    void handle(time_correlated_detection_event<DataTypes> const &event) {
        static_assert(
            std::is_signed_v<typename DataTypes::difftime_type>,
            "difftime_type of time_correlated_detection_event used with negate_difftime must be a signed integer type");
        time_correlated_detection_event<DataTypes> copy(event);
        copy.difftime = -event.difftime;
        downstream.handle(copy);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

template <typename DataTypes, typename Downstream>
class remove_time_correlation {
    Downstream downstream;

  public:
    explicit remove_time_correlation(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "remove_time_correlation");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT>
    void handle(time_correlated_detection_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);

        downstream.handle(
            detection_event<DataTypes>{event.abstime, event.channel});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that changes the sign of difftime in
 * time-correlated detection events.
 *
 * \ingroup processors-time-corr
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::time_correlated_detection_event<DT>`: pass through a copy where
 *   the `difftime` has been negated
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Downstream> auto negate_difftime(Downstream &&downstream) {
    return internal::negate_difftime<Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that removes the difftime from detection events.
 *
 * \ingroup processors-time-corr
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::time_correlated_detection_event<DT>`: emit
 *   `tcspc::detection_event<DataTypes>`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto remove_time_correlation(Downstream &&downstream) {
    return internal::remove_time_correlation<DataTypes, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
