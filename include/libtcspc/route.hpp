/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// Design note: Currently the router produces a single downstream index per
// event. We could generalize this so that the router produces a boolean mask
// of the downstreams, such that a single event can be routed to multiple
// downstreams. However, the exact same effect can also be achieved by adding
// broadcast processors downstream, and it is not clear that there would be
// much of a performance difference. So let's keep it simple.

template <typename EventSetToRoute, typename Router, typename... Downstreams>
class route {
    Router router;
    std::tuple<Downstreams...> downstreams;

    template <typename Event, std::size_t... I>
    void route_event(std::size_t index, Event const &event,
                     [[maybe_unused]] std::index_sequence<I...> seq) {
        (void)std::array{(route_event_if<I>(I == index, event), 0)...};
    }

    template <std::size_t I, typename Event>
    void route_event_if(bool f, Event const &event) {
        if (f) {
            try {
                std::get<I>(downstreams).handle(event);
            } catch (end_processing const &) {
                flush_all_but(std::get<I>(downstreams));
                throw;
            }
        }
    }

    template <typename D> LIBTCSPC_NOINLINE void flush_all_but(D &d) {
        std::apply(
            [&d](auto &...dd) {
                (..., [&](auto &dd) {
                    if (&dd != &d) {
                        try {
                            dd.flush();
                        } catch (end_processing const &) {
                            ;
                        }
                    }
                }(dd));
            },
            downstreams);
    }

    template <typename D> static void do_flush(D &d, std::exception_ptr &end) {
        try {
            d.flush();
        } catch (end_processing const &) {
            if (not end) // Keep only the first end_processing thrown.
                end = std::current_exception();
        }
    }

  public:
    explicit route(Router &&router, Downstreams &&...downstreams)
        : router(router), downstreams{std::move(downstreams)...} {}

    template <typename Event> void handle(Event const &event) {
        if constexpr (contains_event_v<EventSetToRoute, Event>) {
            route_event(router(event), event,
                        std::make_index_sequence<sizeof...(Downstreams)>());
        } else { // Broadcast
            std::apply(
                [&](auto &...d) {
                    (..., [&](auto &d) {
                        try {
                            d.handle(event);
                        } catch (end_processing const &) {
                            flush_all_but(d);
                            throw;
                        }
                    }(d));
                },
                downstreams);
        }
    }

    void flush() {
        std::apply(
            [&](auto &...d) {
                std::exception_ptr end;
                (..., do_flush(d, end));
                if (end)
                    std::rethrow_exception(end);
            },
            downstreams);
    }
};

} // namespace internal

/**
 * \brief Create a processor that routes events to different downstreams.
 *
 * \ingroup processors-timing
 *
 * This processor forwards each event in \c EventSetToRoute to a different
 * downstream according to the provided router.
 *
 * All other events are broadcast to all downstreams.
 *
 * The router must implement the function call operator <tt>auto
 * operator()(Event const &) const noexcept -> std::size_t</tt>, for every \c
 * Event in \c EventSetToRoute, mapping events to downstream index.
 *
 * If the router maps an event to an index beyond the available downstreams,
 * that event is discarded. (Routers can return \c
 * std::numeric_limits<std::size_t>::max() when the event should be discarded.)
 *
 * For routers provided by libtcspc, see \ref routers.
 *
 * \tparam EventSetToRoute event types to route
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
 * \event{Events in EventSetToRoute, routed to at most one of the downstreams}
 * \event{All other events, broadcast to all downstreams}
 * \endevents
 */
template <typename EventSetToRoute, typename Router, typename... Downstreams>
auto route(Router &&router, Downstreams &&...downstreams) {
    return internal::route<EventSetToRoute, Router, Downstreams...>(
        std::forward<Router>(router),
        std::forward<Downstreams>(downstreams)...);
}

/**
 * \brief Router that does not route.
 *
 * \ingroup routers
 *
 * All routed events are discarded when this router is used.
 */
class null_router {
  public:
    /** \brief Router interface. */
    template <typename Event>
    auto operator()([[maybe_unused]] Event const &event) const noexcept
        -> std::size_t {
        return std::size_t(-1);
    }
};

/**
 * \brief Router that routes by channel number.
 *
 * \ingroup routers
 *
 * \tparam N the number of downstreams to route to
 *
 * \tparam DataTraits traits type specifying \c channel_type
 */
template <std::size_t N, typename DataTraits = default_data_traits>
class channel_router {
    std::array<typename DataTraits::channel_type, N> channels;

  public:
    /**
     * \brief Construct with channels to map to downstream indices.
     *
     * \param channels channels in order of downstreams to which to route
     */
    explicit channel_router(
        std::array<typename DataTraits::channel_type, N> const &channels)
        : channels(channels) {}

    /** \brief Router interface. */
    template <typename Event>
    auto operator()(Event const &event) const noexcept -> std::size_t {
        static_assert(std::is_same_v<decltype(event.channel),
                                     typename DataTraits::channel_type>);
        auto it = std::find(channels.begin(), channels.end(), event.channel);
        if (it == channels.end())
            return std::numeric_limits<std::size_t>::max();
        return static_cast<std::size_t>(std::distance(channels.begin(), it));
    }
};

namespace internal {

template <typename Channel> struct channel_router_data_traits {
    using channel_type = Channel;
};

} // namespace internal

/**
 * \brief Deduction guide for array of channels.
 */
template <std::size_t N, typename Channel>
channel_router(std::array<Channel, N>)
    -> channel_router<N, internal::channel_router_data_traits<Channel>>;

/**
 * \brief Create a processor that broadcasts events to multiple downstream
 * processors.
 *
 * \ingroup processors-basic
 *
 * \tparam Downstreams downstream processor classes
 *
 * \param downstreams downstream processors
 *
 * \return broadcast processor
 */
template <typename... Downstreams>
auto broadcast(Downstreams &&...downstreams) {
    return internal::route<event_set<>, null_router, Downstreams...>(
        null_router(), std::forward<Downstreams>(downstreams)...);
}

} // namespace tcspc
