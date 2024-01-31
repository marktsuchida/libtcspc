/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "introspect.hpp"
#include "type_erased_processor.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <numeric>
#include <string>
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

template <typename EventSetToRoute, typename Router, std::size_t N,
          typename Downstream>
class route_homogeneous {
    Router router;
    std::array<Downstream, N> downstreams;

    LIBTCSPC_NOINLINE void flush_all_but(Downstream &excluded) {
        for (auto &d : downstreams) {
            if (&d != &excluded) {
                try {
                    d.flush();
                } catch (end_processing const &) {
                    ;
                }
            }
        }
    }

  public:
    explicit route_homogeneous(Router router,
                               std::array<Downstream, N> downstreams)
        : router(std::move(router)), downstreams(std::move(downstreams)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "route_homogeneous");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return std::transform_reduce(downstreams.begin(), downstreams.end(),
                                     processor_graph(), merge_processor_graphs,
                                     [this](auto const &d) {
                                         auto g = d.introspect_graph();
                                         g.push_entry_point(this);
                                         return g;
                                     });
    }

    template <typename Event> void handle(Event const &event) {
        if constexpr (contains_event_v<EventSetToRoute, Event>) {
            std::size_t index = router(event);
            if (index >= N)
                return;
            try {
                downstreams[index].handle(event);
            } catch (end_processing const &) {
                flush_all_but(downstreams[index]);
                throw;
            }
        } else { // Broadcast
            for (auto &d : downstreams) {
                try {
                    d.handle(event);
                } catch (end_processing const &) {
                    flush_all_but(d);
                    throw;
                }
            }
        }
    }

    void flush() {
        std::exception_ptr end;
        for (auto &d : downstreams) {
            try {
                d.flush();
            } catch (end_processing const &) {
                if (not end)
                    end = std::current_exception();
            }
        }
        if (end)
            std::rethrow_exception(end);
    }
};

} // namespace internal

/**
 * \brief Create a processor that routes events to multiple downstreams of the
 * same type.
 *
 * \ingroup processors-basic
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
 * \see route
 *
 * \tparam EventSetToRoute event types to route
 *
 * \tparam Router type of router (usually deduced)
 *
 * \tparam N number of downstreams
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param router the router
 *
 * \param downstreams downstream processors
 *
 * \return route-homogeneous processor
 */
template <typename EventSetToRoute, typename Router, std::size_t N,
          typename Downstream>
auto route_homogeneous(Router &&router,
                       std::array<Downstream, N> &&downstreams) {
    return internal::route_homogeneous<EventSetToRoute, Router, N, Downstream>(
        std::forward<Router>(router),
        std::forward<std::array<Downstream, N>>(downstreams));
}

/**
 * \brief Create a processor that routes events to different downstreams.
 *
 * \ingroup processors-basic
 *
 * This processor forwards each event in \c EventSetToRoute to a different
 * downstream according to the provided router.
 *
 * All other events (which must be in \c EventSetToBroadcast) are broadcast to
 * all downstreams.
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
 * \see route_homogeneous
 *
 * \tparam EventSetToRoute event types to route
 *
 * \tparam EventSetToBroadcast event types to broadcast
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
 */
template <typename EventSetToRoute, typename EventSetToBroadcast = event_set<>,
          typename Router, typename... Downstreams>
auto route(Router &&router, Downstreams &&...downstreams) {
    using type_erased_downstream = type_erased_processor<
        concat_event_set_t<EventSetToRoute, EventSetToBroadcast>>;
    return route_homogeneous<EventSetToRoute, Router, sizeof...(Downstreams),
                             type_erased_downstream>(
        std::forward<Router>(router),
        std::array<type_erased_downstream, sizeof...(Downstreams)>{
            type_erased_downstream(
                std::forward<Downstreams>(downstreams))...});
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
 * \tparam N the number of channels to route
 *
 * \tparam DataTraits traits type specifying \c channel_type
 */
template <std::size_t N, typename DataTraits = default_data_traits>
class channel_router {
    std::array<typename DataTraits::channel_type, N> channels;
    std::array<std::size_t, N> indices;

  public:
    /**
     * \brief Construct with channels and corresponding downstream indices.
     *
     * \param channel_indices pairs of channels with downstream indices to
     * route to
     */
    template <
        typename Ch, typename I,
        typename = std::enable_if_t<
            std::is_convertible_v<Ch, typename DataTraits::channel_type> &&
            std::is_convertible_v<I, std::size_t>>>
    explicit channel_router(
        std::array<std::pair<Ch, I>, N> const &channel_indices)
        : channels([&] {
              std::array<typename DataTraits::channel_type, N> ret{};
              std::transform(channel_indices.begin(), channel_indices.end(),
                             ret.begin(), [](auto p) { return p.first; });
              return ret;
          }()),
          indices([&] {
              std::array<std::size_t, N> ret{};
              std::transform(channel_indices.begin(), channel_indices.end(),
                             ret.begin(), [](auto p) { return p.second; });
              return ret;
          }()) {}

    /** \brief Router interface. */
    template <typename Event>
    auto operator()(Event const &event) const noexcept -> std::size_t {
        static_assert(std::is_same_v<decltype(event.channel),
                                     typename DataTraits::channel_type>);
        auto it = std::find(channels.begin(), channels.end(), event.channel);
        if (it == channels.end())
            return std::numeric_limits<std::size_t>::max();
        return indices[internal::as_unsigned(
            std::distance(channels.begin(), it))];
    }
};

/**
 * \brief Create a processor that broadcasts events to multiple downstream
 * processors of the same type.
 *
 * \ingroup processors-basic
 *
 * \tparam N number of downstreams (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstreams downstream processors
 *
 * \return broadcast-homogeneous processor
 */
template <std::size_t N, typename Downstream>
auto broadcast_homogeneous(std::array<Downstream, N> &&downstreams) {
    return route_homogeneous<event_set<>, null_router, N, Downstream>(
        null_router(), std::forward<std::array<Downstream, N>>(downstreams));
}

/**
 * \brief Create a processor that broadcasts events to multiple downstream
 * processors.
 *
 * \ingroup processors-basic
 *
 * \tparam EventSetToBroadcast event types to handle
 *
 * \tparam Downstreams downstream processor classes (usually deduced)
 *
 * \param downstreams downstream processors
 *
 * \return broadcast processor
 */
template <typename EventSetToBroadcast, typename... Downstreams>
auto broadcast(Downstreams &&...downstreams) {
    return route<event_set<>, EventSetToBroadcast, null_router,
                 Downstreams...>(null_router(),
                                 std::forward<Downstreams>(downstreams)...);
}

} // namespace tcspc
