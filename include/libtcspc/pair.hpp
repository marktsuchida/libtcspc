/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "time_tagged_events.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <std::size_t NStopChannels, typename DataTypes, typename Downstream>
class pair_all {
    typename DataTypes::channel_type start_chan;
    std::array<typename DataTypes::channel_type, NStopChannels> stop_chans;
    typename DataTypes::abstime_type window_size;

    // Buffer all starts within time window.
    internal::vector_queue<typename DataTypes::abstime_type> starts;

    Downstream downstream;

    void expel_old_starts(typename DataTypes::abstime_type earliest_stop) {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        while (not starts.empty() && starts.front() < cutoff)
            starts.pop();
    }

  public:
    explicit pair_all(
        typename DataTypes::channel_type start_channel,
        std::array<typename DataTypes::channel_type, NStopChannels>
            stop_channels,
        typename DataTypes::abstime_type time_window, Downstream downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        if (window_size < 0)
            throw std::invalid_argument(
                "pair_all time_window must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "pair_all");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(detection_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);

        expel_old_starts(event.abstime);
        auto chan_index = std::distance(
            stop_chans.cbegin(),
            std::find(stop_chans.cbegin(), stop_chans.cend(), event.channel));
        if (std::size_t(chan_index) < NStopChannels) {
            starts.for_each([&](auto start_time) {
                downstream.handle(detection_pair_event<DataTypes>{
                    {start_time, start_chan}, event});
            });
        }
        if (event.channel == start_chan)
            starts.push(event.abstime);
        downstream.handle(event);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

template <std::size_t NStopChannels, typename DataTypes, typename Downstream>
class pair_one {
    typename DataTypes::channel_type start_chan;
    std::array<typename DataTypes::channel_type, NStopChannels> stop_chans;
    typename DataTypes::abstime_type window_size;

    // Buffer all starts within time window, and mark stop channels that have
    // been matched.
    struct start_and_flags {
        typename DataTypes::abstime_type time;
        std::bitset<NStopChannels> stopped;
    };
    internal::vector_queue<start_and_flags> starts;

    Downstream downstream;

    void expel_old_starts(typename DataTypes::abstime_type earliest_stop) {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        while (not starts.empty() &&
               (starts.front().time < cutoff || starts.front().stopped.all()))
            starts.pop();
    }

  public:
    explicit pair_one(
        typename DataTypes::channel_type start_channel,
        std::array<typename DataTypes::channel_type, NStopChannels>
            stop_channels,
        typename DataTypes::abstime_type time_window, Downstream downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        if (window_size < 0)
            throw std::invalid_argument(
                "pair_one time_window must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "pair_one");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(detection_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);

        expel_old_starts(event.abstime);
        auto const chan_index = static_cast<std::size_t>(std::distance(
            stop_chans.cbegin(),
            std::find(stop_chans.cbegin(), stop_chans.cend(), event.channel)));
        if (chan_index < NStopChannels) {
            starts.for_each([&](start_and_flags &sf) {
                if (not sf.stopped[chan_index]) {
                    downstream.handle(detection_pair_event<DataTypes>{
                        {sf.time, start_chan}, event});
                    sf.stopped[chan_index] = true;
                }
            });
        }
        if (event.channel == start_chan)
            starts.push(start_and_flags{event.abstime, {}});
        downstream.handle(event);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

template <std::size_t NStopChannels, typename DataTypes, typename Downstream>
class pair_all_between {
    typename DataTypes::channel_type start_chan;
    std::array<typename DataTypes::channel_type, NStopChannels> stop_chans;
    typename DataTypes::abstime_type window_size;

    // Buffer the most recent start within the time window.
    std::optional<typename DataTypes::abstime_type> start;

    Downstream downstream;

    void expel_old_start(typename DataTypes::abstime_type earliest_stop) {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        if (start.has_value() && *start < cutoff)
            start = std::nullopt;
    }

  public:
    explicit pair_all_between(
        typename DataTypes::channel_type start_channel,
        std::array<typename DataTypes::channel_type, NStopChannels>
            stop_channels,
        typename DataTypes::abstime_type time_window, Downstream downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        if (window_size < 0)
            throw std::invalid_argument(
                "pair_all_between time_window must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "pair_all_between");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(detection_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);

        expel_old_start(event.abstime);
        if (start.has_value()) {
            auto chan_index =
                std::distance(stop_chans.cbegin(),
                              std::find(stop_chans.cbegin(), stop_chans.cend(),
                                        event.channel));
            if (std::size_t(chan_index) < NStopChannels) {
                downstream.handle(detection_pair_event<DataTypes>{
                    {*start, start_chan}, event});
            }
        }
        if (event.channel == start_chan)
            start = event.abstime;
        downstream.handle(event);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

template <std::size_t NStopChannels, typename DataTypes, typename Downstream>
class pair_one_between {
    typename DataTypes::channel_type start_chan;
    std::array<typename DataTypes::channel_type, NStopChannels> stop_chans;
    typename DataTypes::abstime_type window_size;

    // Buffer the most recent start within the time window, and mark stop
    // channels that have been matched.
    struct start_and_flags {
        typename DataTypes::abstime_type time;
        std::bitset<NStopChannels> stopped;
    };
    std::optional<start_and_flags> start;

    Downstream downstream;

    void expel_old_start(typename DataTypes::abstime_type earliest_stop) {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        if (start.has_value() &&
            (start->time < cutoff || start->stopped.all()))
            start = std::nullopt;
    }

  public:
    explicit pair_one_between(
        typename DataTypes::channel_type start_channel,
        std::array<typename DataTypes::channel_type, NStopChannels>
            stop_channels,
        typename DataTypes::abstime_type time_window, Downstream downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        if (window_size < 0)
            throw std::invalid_argument(
                "pair_one_between time_window must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "pair_one_between");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(detection_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTypes::channel_type>);

        expel_old_start(event.abstime);
        if (start.has_value()) {
            auto const chan_index = static_cast<std::size_t>(
                std::distance(stop_chans.cbegin(),
                              std::find(stop_chans.cbegin(), stop_chans.cend(),
                                        event.channel)));
            if (chan_index < NStopChannels && not start->stopped[chan_index]) {
                downstream.handle(detection_pair_event<DataTypes>{
                    {start->time, start_chan}, event});
                start->stopped[chan_index] = true;
            }
        }
        if (event.channel == start_chan)
            start = start_and_flags{event.abstime, {}};
        downstream.handle(event);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that generates all ordered pairs of detection
 * events within a time window.
 *
 * \ingroup processors-pairing
 *
 * All events are passed through.
 *
 * Just before a `tcspc::detection_event` whose channel is one of the stop
 * channels (a stop event) is passed through, a `tcspc::detection_pair_event`
 * is emitted, pairing the stop event with every preceding
 * `tcspc::detection_event` on the start channel that is within \p time_window
 * of the stop event.
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::detection_event<DT>`: emit (as
 *   `tcspc::detection_pair_event<DataTypes>`) any pairs where the current
 *   event serves as the stop event; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <std::size_t NStopChannels, typename DataTypes = default_data_types,
          typename Downstream>
auto pair_all(
    typename DataTypes::channel_type start_channel,
    std::array<typename DataTypes::channel_type, NStopChannels> stop_channels,
    typename DataTypes::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_all<NStopChannels, DataTypes, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that generates ordered pairs of detection events
 * within a time window, pairing only the first eligible stop event with each
 * start event.
 *
 * \ingroup processors-pairing
 *
 * All events are passed through.
 *
 * Just before a `tcspc::detection_event` whose channel is one of the stop
 * channels (a stop event) is passed through, a `tcspc::detection_pair_event`
 * is emitted, pairing the stop event with every preceding
 * `tcspc::detection_event` on the start channel that is within \p time_window
 * of the stop event and more recent than the previous stop event on the same
 * channel.
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::detection_event<DT>`: emit  (as
 *   `tcspc::detection_pair_event<DataTypes>`) any pairs where the current
 *   event serves as the stop event; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <std::size_t NStopChannels, typename DataTypes = default_data_types,
          typename Downstream>
auto pair_one(
    typename DataTypes::channel_type start_channel,
    std::array<typename DataTypes::channel_type, NStopChannels> stop_channels,
    typename DataTypes::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_one<NStopChannels, DataTypes, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that generates ordered pairs of detection events
 * within a time window, pairing only the last eligible start event with each
 * stop event.
 *
 * \ingroup processors-pairing
 *
 * All events are passed through.
 *
 * Just before a `tcspc::detection_event` whose channel is one of the stop
 * channels (a stop event) is passed through, a `tcspc::detection_pair_event`
 * is emitted, pairing the stop event with the most recent
 * `tcspc::detection_event` on the start channel, if there is one within \p
 * time_window of the stop event.
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::detection_event<DT>`: emit (as
 *   `tcspc::detection_pair_event<DataTypes>`), if any, the pair where the
 *   current event serves as the stop event; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <std::size_t NStopChannels, typename DataTypes = default_data_types,
          typename Downstream>
auto pair_all_between(
    typename DataTypes::channel_type start_channel,
    std::array<typename DataTypes::channel_type, NStopChannels> stop_channels,
    typename DataTypes::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_all_between<NStopChannels, DataTypes, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that generates ordered pairs of detection events
 * within a time window, pairing only a start event with a stop event such that
 * no start events, or stop events on the same channel, occur in between.
 *
 * \ingroup processors-pairing
 *
 * All events are passed through.
 *
 * Just before a `tcspc::detection_event` whose channel is one of the stop
 * channels (a stop event) is passed through, a `tcspc::detection_pair_event`
 * is emitted, pairing the stop event with the most recent
 * `tcspc::detection_event` on the start channel, if there is one within \p
 * time_window of the stop event and more recent than the previous stop event
 * on the same channel.
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::detection_event<DT>`: emit (as
 *   `tcspc::detection_pair_event<DataTypes>`), if any, the pair where the
 *   current event serves as the stop event; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <std::size_t NStopChannels, typename DataTypes = default_data_types,
          typename Downstream>
auto pair_one_between(
    typename DataTypes::channel_type start_channel,
    std::array<typename DataTypes::channel_type, NStopChannels> stop_channels,
    typename DataTypes::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_one_between<NStopChannels, DataTypes, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
