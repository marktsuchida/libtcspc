/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "time_tagged_events.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <exception>
#include <iterator>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename T>
constexpr auto pairing_cutoff(T stop_time, T window_size) noexcept {
    // Guard against underflow (window_size is non-negative).
    if (stop_time < std::numeric_limits<T>::min() + window_size)
        return std::numeric_limits<T>::min();
    return stop_time - window_size;
}

template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
class pair_all {
    typename DataTraits::channel_type start_chan;
    std::array<typename DataTraits::channel_type, NStopChannels> stop_chans;
    typename DataTraits::abstime_type window_size;

    // Buffer all starts within time window.
    internal::vector_queue<typename DataTraits::abstime_type> starts;

    Downstream downstream;

    void expel_old_starts(
        typename DataTraits::abstime_type earliest_stop) noexcept {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        while (not starts.empty() && starts.front() < cutoff)
            starts.pop();
    }

  public:
    explicit pair_all(
        typename DataTraits::channel_type start_channel,
        std::array<typename DataTraits::channel_type, NStopChannels>
            stop_channels,
        typename DataTraits::abstime_type time_window, Downstream &&downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        assert(window_size >= 0);
    }

    template <typename DT>
    void handle_event(detection_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);

        expel_old_starts(event.abstime);
        auto chan_index = std::distance(
            stop_chans.cbegin(),
            std::find(stop_chans.cbegin(), stop_chans.cend(), event.channel));
        if (std::size_t(chan_index) < NStopChannels) {
            starts.for_each([&](auto start_time) noexcept {
                downstream.handle_event(detection_pair_event<DataTraits>{
                    {{{start_time}, start_chan}}, event});
            });
        }
        if (event.channel == start_chan)
            starts.push(event.abstime);
        downstream.handle_event(event);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
class pair_one {
    typename DataTraits::channel_type start_chan;
    std::array<typename DataTraits::channel_type, NStopChannels> stop_chans;
    typename DataTraits::abstime_type window_size;

    // Buffer all starts within time window, and mark stop channels that have
    // been matched.
    struct start_and_flags {
        typename DataTraits::abstime_type time;
        std::bitset<NStopChannels> stopped;
    };
    internal::vector_queue<start_and_flags> starts;

    Downstream downstream;

    void expel_old_starts(
        typename DataTraits::abstime_type earliest_stop) noexcept {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        while (not starts.empty() &&
               (starts.front().time < cutoff || starts.front().stopped.all()))
            starts.pop();
    }

  public:
    explicit pair_one(
        typename DataTraits::channel_type start_channel,
        std::array<typename DataTraits::channel_type, NStopChannels>
            stop_channels,
        typename DataTraits::abstime_type time_window, Downstream &&downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        assert(window_size >= 0);
    }

    template <typename DT>
    void handle_event(detection_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);

        expel_old_starts(event.abstime);
        auto chan_index = std::distance(
            stop_chans.cbegin(),
            std::find(stop_chans.cbegin(), stop_chans.cend(), event.channel));
        if (std::size_t(chan_index) < NStopChannels) {
            starts.for_each([&](start_and_flags &sf) noexcept {
                if (not sf.stopped[chan_index]) {
                    downstream.handle_event(detection_pair_event<DataTraits>{
                        {{{sf.time}, start_chan}}, event});
                    sf.stopped[chan_index] = true;
                }
            });
        }
        if (event.channel == start_chan)
            starts.push(start_and_flags{event.abstime, {}});
        downstream.handle_event(event);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
class pair_all_between {
    typename DataTraits::channel_type start_chan;
    std::array<typename DataTraits::channel_type, NStopChannels> stop_chans;
    typename DataTraits::abstime_type window_size;

    // Buffer the most recent start within the time window.
    std::optional<typename DataTraits::abstime_type> start;

    Downstream downstream;

    void
    expel_old_start(typename DataTraits::abstime_type earliest_stop) noexcept {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        if (start.has_value() && *start < cutoff)
            start = std::nullopt;
    }

  public:
    explicit pair_all_between(
        typename DataTraits::channel_type start_channel,
        std::array<typename DataTraits::channel_type, NStopChannels>
            stop_channels,
        typename DataTraits::abstime_type time_window, Downstream &&downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        assert(window_size >= 0);
    }

    template <typename DT>
    void handle_event(detection_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);

        expel_old_start(event.abstime);
        if (start.has_value()) {
            auto chan_index =
                std::distance(stop_chans.cbegin(),
                              std::find(stop_chans.cbegin(), stop_chans.cend(),
                                        event.channel));
            if (std::size_t(chan_index) < NStopChannels) {
                downstream.handle_event(detection_pair_event<DataTraits>{
                    {{{*start}, start_chan}}, event});
            }
        }
        if (event.channel == start_chan)
            start = event.abstime;
        downstream.handle_event(event);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
class pair_one_between {
    typename DataTraits::channel_type start_chan;
    std::array<typename DataTraits::channel_type, NStopChannels> stop_chans;
    typename DataTraits::abstime_type window_size;

    // Buffer the most recent start within the time window, and mark stop
    // channels that have been matched.
    struct start_and_flags {
        typename DataTraits::abstime_type time;
        std::bitset<NStopChannels> stopped;
    };
    std::optional<start_and_flags> start;

    Downstream downstream;

    void
    expel_old_start(typename DataTraits::abstime_type earliest_stop) noexcept {
        auto const cutoff = pairing_cutoff(earliest_stop, window_size);
        if (start.has_value() &&
            (start->time < cutoff || start->stopped.all()))
            start = std::nullopt;
    }

  public:
    explicit pair_one_between(
        typename DataTraits::channel_type start_channel,
        std::array<typename DataTraits::channel_type, NStopChannels>
            stop_channels,
        typename DataTraits::abstime_type time_window, Downstream &&downstream)
        : start_chan(start_channel), stop_chans(stop_channels),
          window_size(time_window), downstream(std::move(downstream)) {
        assert(window_size >= 0);
    }

    template <typename DT>
    void handle_event(detection_event<DT> const &event) noexcept {
        static_assert(std::is_same_v<typename DT::abstime_type,
                                     typename DataTraits::abstime_type>);
        static_assert(std::is_same_v<typename DT::channel_type,
                                     typename DataTraits::channel_type>);

        expel_old_start(event.abstime);
        if (start.has_value()) {
            auto chan_index =
                std::distance(stop_chans.cbegin(),
                              std::find(stop_chans.cbegin(), stop_chans.cend(),
                                        event.channel));
            if (std::size_t(chan_index) < NStopChannels &&
                not start->stopped[chan_index]) {
                downstream.handle_event(detection_pair_event<DataTraits>{
                    {{{start->time}, start_chan}}, event});
                start->stopped[chan_index] = true;
            }
        }
        if (event.channel == start_chan)
            start = start_and_flags{event.abstime, {}};
        downstream.handle_event(event);
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
 * \brief Create a processor that generates all ordered pairs of detection
 * events within a time window.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Just before a \ref detection_event whose channel is one of the stop channels
 * (a stop event) is passed through, a \ref detection_pair_event is emitted,
 * pairing the stop event with every preceding \ref detection_event on the
 * start channel that is within \e time_window of the stop event.
 *
 * \see pair_one
 * \see pair_all_between
 * \see pair_one_between
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam Downstream downstream processor type
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 */
template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
auto pair_all(
    typename DataTraits::channel_type start_channel,
    std::array<typename DataTraits::channel_type, NStopChannels> stop_channels,
    typename DataTraits::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_all<DataTraits, NStopChannels, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that generates ordered pairs of detection events
 * within a time window, pairing only the first eligible stop event with each
 * start event.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Just before a \ref detection_event whose channel is one of the stop channels
 * (a stop event) is passed through, a \ref detection_pair_event is emitted,
 * pairing the stop event with every preceding \ref detection_event on the
 * start channel that is within \e time_window of the stop event and more
 * recent than the previous stop event on the same channel.
 *
 * \see pair_all
 * \see pair_all_between
 * \see pair_one_between
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam Downstream downstream processor type
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 */
template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
auto pair_one(
    typename DataTraits::channel_type start_channel,
    std::array<typename DataTraits::channel_type, NStopChannels> stop_channels,
    typename DataTraits::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_one<DataTraits, NStopChannels, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that generates ordered pairs of detection events
 * within a time window, pairing only the last eligible start event with each
 * stop event.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Just before a \ref detection_event whose channel is one of the stop channels
 * (a stop event) is passed through, a \ref detection_pair_event is emitted,
 * pairing the stop event with the most recent \ref detection_event on the
 * start channel, if there is one within \e time_window of the stop event.
 *
 * \see pair_all
 * \see pair_one
 * \see pair_one_between
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam Downstream downstream processor type
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 */
template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
auto pair_all_between(
    typename DataTraits::channel_type start_channel,
    std::array<typename DataTraits::channel_type, NStopChannels> stop_channels,
    typename DataTraits::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_all_between<DataTraits, NStopChannels, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that generates ordered pairs of detection events
 * within a time window, pairing only a start event with a stop event such that
 * no start events, or stop events on the same channel, occur in between.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Just before a \ref detection_event whose channel is one of the stop channels
 * (a stop event) is passed through, a \ref detection_pair_event is emitted,
 * pairing the stop event with the most recent \ref detection_event on the
 * start channel, if there is one within \e time_window of the stop event and
 * more recent than the previous stop event on the same channel.
 *
 * \see pair_all
 * \see pair_one
 * \see pair_all_between
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 *
 * \tparam NStopChannels number of stop channels
 *
 * \tparam Downstream downstream processor type
 *
 * \param start_channel the start channel number
 *
 * \param stop_channels the stop channel numbers
 *
 * \param time_window the maximum abstime separateion between start and stop
 * detection events
 *
 * \param downstream downstream processor
 */
template <typename DataTraits, std::size_t NStopChannels, typename Downstream>
auto pair_one_between(
    typename DataTraits::channel_type start_channel,
    std::array<typename DataTraits::channel_type, NStopChannels> stop_channels,
    typename DataTraits::abstime_type time_window, Downstream &&downstream) {
    return internal::pair_one_between<DataTraits, NStopChannels, Downstream>(
        start_channel, stop_channels, time_window,
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
