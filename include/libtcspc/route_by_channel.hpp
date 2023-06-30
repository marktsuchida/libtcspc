/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <tuple>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventToRoute, typename... Downstreams>
class route_by_channel {
    std::array<std::int16_t, sizeof...(Downstreams)> channels;
    std::tuple<Downstreams...> downstreams;

    template <std::size_t I = 0>
    void handle_photon(std::size_t index, EventToRoute const &event) noexcept {
        if (index == I) {
            std::get<I>(downstreams).handle_event(event);
            return;
        }
        if constexpr (I + 1 < std::tuple_size_v<decltype(downstreams)>)
            handle_photon<I + 1>(index, event);
    }

  public:
    explicit route_by_channel(
        std::array<std::int16_t, sizeof...(Downstreams)> const &channels,
        Downstreams &&...downstreams)
        : channels(channels), downstreams{std::move(downstreams)...} {}

    void handle_event(EventToRoute const &event) noexcept {
        auto chan = event.channel;
        auto it = std::find(channels.cbegin(), channels.cend(), chan);
        if (it != channels.cend())
            handle_photon(as_unsigned(std::distance(channels.cbegin(), it)),
                          event);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        std::apply([&](auto &...s) { (..., s.handle_event(event)); },
                   downstreams);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        std::apply([&](auto &...s) { (..., s.handle_end(error)); },
                   downstreams);
    }
};

} // namespace internal

/**
 * \brief Create a processor that routes events to downstream processors
 * according to channel.
 *
 * \ingroup processors-timing
 *
 * This processor holds multiple downstream processors and a mapping from
 * channel numbers to downstream indices. Events of type \c ERoute are passed
 * only to the downstream indexed by the mapped channel number.
 *
 * If the channel does not map to a downstream index, then the \c ERoute event
 * is discarded.
 *
 * Events other than \c ERoute are broadcast to all downstream processors.
 *
 * The channel mapping is specified as an std::array of channel numbers.
 * The channel at index \e i in the array is mapped to downstream index \e i.
 * (This has the limitation that only one channel can be mapped to each
 * downstream.)
 *
 * Thus, if channels contains <tt>{5, -3}</tt> and an \c EventToRoute event is
 * received with channel equal to \c -3, then it is routed to downstream
 * processor 1 (counting from 0).
 *
 * \tparam EventToRoute event type to route by channel
 *
 * \tparam Downstreams downstream processor types
 *
 * \param channels channel mapping
 *
 * \param downstreams downstream processors (moved out)
 *
 * \return route-by-channel processor
 */
template <typename EventToRoute, typename... Downstreams>
auto route_by_channel(
    std::array<std::int16_t, sizeof...(Downstreams)> const &channels,
    Downstreams &&...downstreams) {
    return internal::route_by_channel<EventToRoute, Downstreams...>(
        channels, std::forward<Downstreams>(downstreams)...);
}

} // namespace tcspc