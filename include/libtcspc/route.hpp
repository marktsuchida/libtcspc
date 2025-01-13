/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "data_types.hpp"
#include "errors.hpp"
#include "int_arith.hpp"
#include "introspect.hpp"
#include "type_erased_processor.hpp"
#include "type_list.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <numeric>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// Design note: Currently the router produces a single downstream index per
// event. We could generalize this so that the router produces a boolean mask
// of the downstreams, such that a single event can be routed to multiple
// downstreams. But let's keep it simple. If necessary, a "multiroute"
// processor can be added.

template <typename RoutedEventList, typename Router, std::size_t N,
          typename Downstream>
class route_homogeneous {
    static_assert(is_type_list_v<RoutedEventList>);
    // We do not require Downstream to handle all of RoutedEventList.
    static_assert(is_processor_v<Downstream>);

    Router router;
    std::array<Downstream, N> downstreams;

    LIBTCSPC_NOINLINE void flush_all_but(Downstream &excluded) {
        for (auto &d : downstreams) {
            if (&d != &excluded) {
                try {
                    d.flush();
                } catch (end_of_processing const &) {
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
        return processor_info(this, "route_homogeneous");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return std::transform_reduce(
            downstreams.begin(), downstreams.end(), processor_graph(),
            merge_processor_graphs, [this](auto const &d) {
                return d.introspect_graph().push_entry_point(this);
            });
    }

    template <typename Event, typename = std::enable_if_t<handles_event_v<
                                  Downstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        if constexpr (is_convertible_to_type_list_member_v<
                          remove_cvref_t<Event>, RoutedEventList>) {
            std::size_t index = router(std::as_const(event));
            if (index >= N)
                return;
            try {
                downstreams[index].handle(std::forward<Event>(event));
            } catch (end_of_processing const &) {
                flush_all_but(downstreams[index]);
                throw;
            }
        } else { // Broadcast
            for (auto &d : downstreams) {
                try {
                    d.handle(std::as_const(event));
                } catch (end_of_processing const &) {
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
            } catch (end_of_processing const &) {
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
 * \ingroup processors-branching
 *
 * This processor forwards each event in \p RoutedEventList to a different
 * downstream according to the provided \p router (see \ref routers), which map
 * events to downstream indices.
 *
 * Events mapped to indices out of range are discarded.
 *
 * All other events are broadcast to all downstreams.
 *
 * \see `tcspc::route()`
 * \see `tcspc::broadcast_homogeneous()`
 *
 * \tparam RoutedEventList event types to route
 *
 * \tparam Router type of router (usually deduced)
 *
 * \tparam N number of downstreams (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param router the router
 *
 * \param downstreams downstream processors
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `RoutedEventList`: invoke router; pass to downstream at the
 *   resulting index, or ignore if out of range
 * - Types not in `RoutedEventList`: broadcast to every downstream
 * - Flush: broadcast to every downstream
 */
template <typename RoutedEventList, typename Router, std::size_t N,
          typename Downstream>
auto route_homogeneous(Router router, std::array<Downstream, N> downstreams) {
    return internal::route_homogeneous<RoutedEventList, Router, N, Downstream>(
        std::move(router), std::move(downstreams));
}

/**
 * \brief Create a processor that routes events to multiple downstreams of the
 * same type.
 *
 * \ingroup processors-branching
 *
 * This processor forwards each event in \p RoutedEventList to a different
 * downstream according to the provided \p router (see \ref routers), which map
 * events to downstream indices.
 *
 * Events mapped to indices out of range are discarded.
 *
 * All other events are broadcast to all downstreams.
 *
 * \see `tcspc::route()`
 * \see `tcspc::broadcast_homogeneous()`
 *
 * \tparam RoutedEventList event types to route
 *
 * \tparam Router type of router (usually deduced)
 *
 * \tparam Downstreams downstream processor types (usually deduced; must be all
 * equal)
 *
 * \param router the router
 *
 * \param downstreams downstream processors
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `RoutedEventList`: invoke router; pass to downstream at the
 *   resulting index, or ignore if out of range
 * - Types not in `RoutedEventList`: broadcast to every downstream
 * - Flush: broadcast to every downstream
 */
template <typename RoutedEventList, typename Router, typename... Downstreams>
auto route_homogeneous(Router router, Downstreams... downstreams) {
    return route_homogeneous<RoutedEventList, Router>(
        std::move(router), std::array{std::move(downstreams)...});
}

/**
 * \brief Create a processor that routes events to different downstreams.
 *
 * \ingroup processors-branching
 *
 * This processor forwards each event in \p RoutedEventList to a different
 * downstream according to the provided \p router (see \ref routers), which map
 * events to downstream indices.
 *
 * Events mapped to indices out of range are discarded.
 *
 * All other events (which must be in \p BroadcastEventList) are broadcast to
 * all downstreams.
 *
 * \see `tcspc::route_homogeneous()`
 * \see `tcspc::broadcast()`
 *
 * \tparam RoutedEventList event types to route
 *
 * \tparam BroadcastEventList event types to broadcast
 *
 * \tparam Router type of router (usually deduced)
 *
 * \tparam Downstreams downstream processor types (usually deduced)
 *
 * \param router the router
 *
 * \param downstreams downstream processors
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `RoutedEventList`: invoke router; pass to downstream at the
 *   resulting index, or ignore if out of range
 * - Types not in `RoutedEventList` but in `BroadcastEventList`: broadcast to
 *   every downstream
 * - Flush: broadcast to every downstream
 */
template <typename RoutedEventList, typename BroadcastEventList = type_list<>,
          typename Router, typename... Downstreams>
auto route(Router router, Downstreams... downstreams) {
    static_assert(is_type_list_v<RoutedEventList>);
    static_assert(is_type_list_v<BroadcastEventList>);
    static_assert(
        type_list_size_v<
            type_list_intersection_t<RoutedEventList, BroadcastEventList>> ==
            0,
        "routed event list and broadcast event list must not overlap");
    using type_erased_downstream = type_erased_processor<
        type_list_union_t<RoutedEventList, BroadcastEventList>>;
    return route_homogeneous<RoutedEventList, Router, sizeof...(Downstreams),
                             type_erased_downstream>(
        std::move(router),
        std::array<type_erased_downstream, sizeof...(Downstreams)>{
            type_erased_downstream(std::move(downstreams))...});
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
    /**
     * \brief Implements router requirement; always returns
     * `std::numeric_limits<std::size_t>::max()`.
     */
    template <typename Event>
    auto operator()(Event const & /* event */) const -> std::size_t {
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
 * \tparam DataTypes data type set specifying `channel_type`
 */
template <std::size_t N, typename DataTypes = default_data_types>
class channel_router {
    std::array<typename DataTypes::channel_type, N> channels;
    std::array<std::size_t, N> indices;

  public:
    /**
     * \brief Construct with channels and corresponding downstream indices.
     *
     * \tparam ChannelIndexPair tuple-like type (`DataTypes::channel_type`,
     * `std::size_t`) of \p channel_indices (usually deduced)
     *
     * \param channel_indices pairs of channels with downstream indices to
     * route to
     */
    template <typename ChannelIndexPair>
    explicit channel_router(
        std::array<ChannelIndexPair, N> const &channel_indices)
        : channels(std::invoke([&] {
              std::array<typename DataTypes::channel_type, N> ret{};
              std::transform(channel_indices.begin(), channel_indices.end(),
                             ret.begin(),
                             [](auto p) { return std::get<0>(p); });
              return ret;
          })),
          indices(std::invoke([&] {
              std::array<std::size_t, N> ret{};
              std::transform(channel_indices.begin(), channel_indices.end(),
                             ret.begin(),
                             [](auto p) { return std::get<1>(p); });
              return ret;
          })) {

        static_assert(
            std::is_convertible_v<decltype(std::get<0>(channel_indices[0])),
                                  typename DataTypes::channel_type> &&
                std::is_convertible_v<
                    decltype(std::get<1>(channel_indices[0])), std::size_t>,
            "channel_indices must be an array of pair-like convertible to (channel, std::size_t)");
    }

    /** \brief Implements router requirement. */
    template <typename Event>
    auto operator()(Event const &event) const -> std::size_t {
        static_assert(std::is_same_v<decltype(event.channel),
                                     typename DataTypes::channel_type>);
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
 * \ingroup processors-branching
 *
 * \see `tcspc::broadcast()`
 * \see `tcspc::route_homogeneous()`
 *
 * \tparam N number of downstreams (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstreams downstream processors
 *
 * \return processor
 *
 * \par Events handled
 * - All types: broadcast to every downstream
 * - Flush: broadcast to every downstream
 */
template <std::size_t N, typename Downstream>
auto broadcast_homogeneous(std::array<Downstream, N> downstreams) {
    return route_homogeneous<type_list<>, null_router, N, Downstream>(
        null_router(), std::move(downstreams));
}

/**
 * \brief Create a processor that broadcasts events to multiple downstream
 * processors of the same type.
 *
 * \ingroup processors-branching
 *
 * \see `tcspc::broadcast()`
 * \see `tcspc::route_homogeneous()`
 *
 * \tparam Downstreams downstream processor types (usually deduced; must be all
 * equal)
 *
 * \param downstreams downstream processors
 *
 * \return processor
 *
 * \par Events handled
 * - All types: broadcast to every downstream
 * - Flush: broadcast to every downstream
 */
template <typename... Downstreams>
auto broadcast_homogeneous(Downstreams... downstreams) {
    auto arr = std::array{std::move(downstreams)...};
    return broadcast_homogeneous(std::move(arr));
}

/**
 * \brief Create a processor that broadcasts events to multiple downstream
 * processors.
 *
 * \ingroup processors-branching
 *
 * \see `tcspc::broadcast_homogeneous()`
 * \see `tcspc::route()`
 *
 * \tparam BroadcastEventList event types to handle
 *
 * \tparam Downstreams downstream processor classes (usually deduced)
 *
 * \param downstreams downstream processors
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `BroadcastEventList`: broadcast to every downstream
 * - Flush: broadcast to every downstream
 */
template <typename BroadcastEventList, typename... Downstreams>
auto broadcast(Downstreams... downstreams) {
    return route<type_list<>, BroadcastEventList, null_router, Downstreams...>(
        null_router(), std::move(downstreams)...);
}

} // namespace tcspc
