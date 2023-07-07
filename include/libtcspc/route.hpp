/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <tuple>
#include <utility>

namespace tcspc {

namespace internal {

// Design note: Currently the router produces a single downstream index per
// event. We could generalize this so that the router produces a boolean mask
// of the downstreams, such that a single event can be routed to multiple
// downstreams. However, the exact same effect can also be achieved by adding
// broadcast processors downstream, and it is not clear that there would be
// much of a performance difference. So let's keep it simple.

template <typename EventToRoute, typename Router, typename... Downstreams>
class route {
    Router router;
    std::tuple<Downstreams...> downstreams;

    template <std::size_t I>
    void route_event_if(bool f, EventToRoute const &event) noexcept {
        if (f)
            std::get<I>(downstreams).handle_event(event);
    }

    template <std::size_t... I>
    void route_event(std::size_t index, EventToRoute const &event,
                     std::index_sequence<I...>) noexcept {
        (void)std::array{(route_event_if<I>(I == index, event), 0)...};
    }

  public:
    explicit route(Router &&router, Downstreams &&...downstreams)
        : router(router), downstreams{std::move(downstreams)...} {}

    void handle_event(EventToRoute const &event) noexcept {
        route_event(router(event), event,
                    std::make_index_sequence<sizeof...(Downstreams)>());
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
 * \brief Create a processor that routes events to different downstreams.
 *
 * \ingroup processors-timing
 *
 * This processor forwards each event of type \c EventToRoute to a different
 * downstream according to the provided router.
 *
 * All other events are broadcast to all downstreams.
 *
 * The router must implement the function call operator <tt>auto
 * operator()(EventToRoute const &) const noexcept -> std::size_t</tt>, mapping
 * events to downstream index.
 *
 * If the router maps an event to an index beyond the available downstreams,
 * that event is discarded. (Routers can return \c
 * std::numeric_limits<std::size_t>::max() when the event should be discarded.)
 *
 * For routers provided by libtcspc, see \ref routers.
 *
 * \tparam EventToRoute event type to route
 *
 * \tparam Router type of router
 *
 * \tparam Downstreams downstream processor types
 *
 * \param router the router
 *
 * \param downstreams downstream processors
 *
 * \return route processor
 *
 * \inevents
 * \event{EventToRoute, routed to at most one of the downstreams}
 * \event{All other events, broadcast to all downstreams}
 * \endevents
 */
template <typename EventToRoute, typename Router, typename... Downstreams>
auto route(Router &&router, Downstreams &&...downstreams) {
    return internal::route<EventToRoute, Router, Downstreams...>(
        std::forward<Router>(router),
        std::forward<Downstreams>(downstreams)...);
}

/**
 * \brief Router that routes by channel number.
 *
 * \ingroup routers
 *
 * \tparam N the number of downstreams to route to
 */
template <std::size_t N> class channel_router {
    std::array<std::int16_t, N> channels;

  public:
    /**
     * \brief Construct with channels to map to downstream indices.
     *
     * \param channels channels in order of downstreams to which to route
     */
    explicit channel_router(std::array<std::int16_t, N> const &channels)
        : channels(channels) {}

    /** \brief Router interface. */
    template <typename Event>
    auto operator()(Event const &event) const noexcept -> std::size_t {
        auto it = std::find(channels.begin(), channels.end(), event.channel);
        if (it == channels.end())
            return std::numeric_limits<std::size_t>::max();
        return std::distance(channels.begin(), it);
    }
};

/**
 * \brief Deduction guide for array of channels.
 */
template <std::size_t N>
channel_router(std::array<std::int16_t, N>) -> channel_router<N>;

} // namespace tcspc
