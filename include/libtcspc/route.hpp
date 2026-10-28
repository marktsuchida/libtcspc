/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "errors.hpp"
#include "int_arith.hpp"
#include "introspect.hpp"
#include "numeric_traits.hpp"
#include "processor.hpp"
#include "type_list.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <utility>

namespace tcspc {

/**
 * \brief Concept that is satisfied when \p R conforms to the libtcspc router
 * interface for event types \p Events.
 *
 * \ingroup routers
 *
 * Determines whether \p R is move-constructible and provides
 * `operator()(Event const &) const` returning `std::size_t` for every `Event`
 * in \p Events.
 */
template <typename R, typename... Events>
concept router_for = std::move_constructible<R> &&
                     (... && requires(R const &r, Events const &e) {
                         { r(e) } -> std::same_as<std::size_t>;
                     });

namespace internal {

// Design note: Currently the router produces a single downstream index per
// event. We could generalize this so that the router produces a boolean mask
// of the downstreams, such that a single event can be routed to multiple
// downstreams. But let's keep it simple. If necessary, a "multiroute"
// processor can be added.

template <typename Router, typename EventList> struct router_for_each_event;

template <typename Router, typename... Events>
struct router_for_each_event<Router, type_list<Events...>>
    : std::bool_constant<router_for<Router, Events...>> {};

template <typename RoutedEventList, typename BroadcastEventList,
          typename Router, std::size_t N, typename Downstream>
    requires type_list_like<RoutedEventList> &&
             type_list_like<BroadcastEventList> &&
             router_for_each_event<Router, RoutedEventList>::value &&
             is_processor_of_list_v<
                 Downstream,
                 type_list_union_t<RoutedEventList, BroadcastEventList>>
class route_homogeneous {
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

    template <typename Event>
        requires(convertible_to_type_list_member<std::remove_cvref_t<Event>,
                                                 RoutedEventList> and
                 handler_for<Downstream, std::remove_cvref_t<Event>>)
    void handle(Event &&event) {
        std::size_t index = router(std::as_const(event));
        if (index >= N)
            return;
        try {
            downstreams[index].handle(std::forward<Event>(event));
        } catch (end_of_processing const &) {
            flush_all_but(downstreams[index]);
            throw;
        }
    }

    template <typename Event>
        requires(not convertible_to_type_list_member<
                     std::remove_cvref_t<Event>, RoutedEventList> and
                 convertible_to_type_list_member<std::remove_cvref_t<Event>,
                                                 BroadcastEventList> and
                 handler_for<Downstream, std::remove_cvref_t<Event>>)
    void handle(Event &&event) {
        for (auto &d : downstreams) {
            try {
                d.handle(std::as_const(event));
            } catch (end_of_processing const &) {
                flush_all_but(d);
                throw;
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

template <typename RoutedEventList, typename BroadcastEventList,
          typename Router, typename... Downstreams>
    requires type_list_like<RoutedEventList> &&
             type_list_like<BroadcastEventList> &&
             router_for_each_event<Router, RoutedEventList>::value &&
             (is_processor_of_list_v<
                  Downstreams,
                  type_list_union_t<RoutedEventList, BroadcastEventList>> &&
              ...)
class route_heterogeneous {
    static constexpr std::size_t N = sizeof...(Downstreams);
    using downstreams_type = std::tuple<Downstreams...>;

    Router router;
    downstreams_type downstreams;

    template <typename F> void for_each_downstream(F f) {
        [&]<std::size_t... Is>(std::index_sequence<Is...> /* indices */) {
            (f(Is, std::get<Is>(downstreams)), ...);
        }(std::make_index_sequence<N>());
    }

    template <typename Event, std::size_t... Is>
    static constexpr auto
    make_route_table(std::index_sequence<Is...> /* indices */) {
        return std::array<void (*)(downstreams_type &, Event &&), N>{
            [](downstreams_type &ds, Event &&e) {
                std::get<Is>(ds).handle(std::forward<Event>(e));
            }...};
    }

    LIBTCSPC_NOINLINE void flush_all_but(std::size_t excluded) {
        for_each_downstream([&](std::size_t i, auto &d) {
            if (i != excluded) {
                try {
                    d.flush();
                } catch (end_of_processing const &) {
                    ;
                }
            }
        });
    }

  public:
    explicit route_heterogeneous(Router router, Downstreams... downstreams)
        : router(std::move(router)), downstreams(std::move(downstreams)...) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "route_heterogeneous");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return std::apply(
            [this](auto const &...ds) {
                processor_graph g;
                ((g = merge_processor_graphs(
                      g, ds.introspect_graph().push_entry_point(this))),
                 ...);
                return g;
            },
            downstreams);
    }

    template <typename Event>
        requires(convertible_to_type_list_member<std::remove_cvref_t<Event>,
                                                 RoutedEventList> and
                 (handler_for<Downstreams, std::remove_cvref_t<Event>> && ...))
    void handle(Event &&event) {
        static constexpr auto route_table =
            make_route_table<Event>(std::make_index_sequence<N>());
        std::size_t index = router(std::as_const(event));
        if (index >= N)
            return;
        try {
            route_table[index](downstreams, std::forward<Event>(event));
        } catch (end_of_processing const &) {
            flush_all_but(index);
            throw;
        }
    }

    template <typename Event>
        requires(not convertible_to_type_list_member<
                     std::remove_cvref_t<Event>, RoutedEventList> and
                 convertible_to_type_list_member<std::remove_cvref_t<Event>,
                                                 BroadcastEventList> and
                 (handler_for<Downstreams, std::remove_cvref_t<Event>> && ...))
    void handle(Event &&event) {
        for_each_downstream([&](std::size_t i, auto &d) {
            try {
                d.handle(std::as_const(event));
            } catch (end_of_processing const &) {
                flush_all_but(i);
                throw;
            }
        });
    }

    void flush() {
        std::exception_ptr end;
        for_each_downstream([&](std::size_t /* i */, auto &d) {
            try {
                d.flush();
            } catch (end_of_processing const &) {
                if (not end)
                    end = std::current_exception();
            }
        });
        if (end)
            std::rethrow_exception(end);
    }
};

template <typename... Ts> struct all_same : std::true_type {};

template <typename T, typename... Ts>
struct all_same<T, Ts...> : std::bool_constant<(std::same_as<T, Ts> && ...)> {
};

template <typename RoutedEventList, typename BroadcastEventList>
inline constexpr bool route_event_lists_disjoint_v =
    type_list_size_v<
        type_list_intersection_t<RoutedEventList, BroadcastEventList>> == 0;

} // namespace internal

/**
 * \brief Create a processor that routes events to multiple downstreams of the
 * same type.
 *
 * \ingroup processors-branching
 *
 * This processor forwards each event in \p RoutedEventList to a different
 * downstream according to the provided \p router (see \ref routers), which
 * maps events to downstream indices.
 *
 * Events mapped to indices out of range are discarded.
 *
 * All other events (which must be in \p BroadcastEventList) are broadcast to
 * all downstreams.
 *
 * Every downstream must handle all events in \p RoutedEventList and \p
 * BroadcastEventList.
 *
 * This overload is equivalent to the variadic overload given downstreams of
 * identical type.
 *
 * \see `tcspc::broadcast()`
 *
 * \tparam RoutedEventList event types to route
 *
 * \tparam BroadcastEventList event types to broadcast
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
 * - Types not in `RoutedEventList` but in `BroadcastEventList`: broadcast to
 *   every downstream
 * - Flush: broadcast to every downstream
 */
template <typename RoutedEventList, typename BroadcastEventList = type_list<>,
          typename Router, std::size_t N, typename Downstream>
    requires is_processor_of_list_v<
        Downstream, type_list_union_t<RoutedEventList, BroadcastEventList>>
auto route(Router router, std::array<Downstream, N> downstreams) {
    static_assert(
        internal::route_event_lists_disjoint_v<RoutedEventList,
                                               BroadcastEventList>,
        "routed event list and broadcast event list must not overlap");
    return internal::route_homogeneous<RoutedEventList, BroadcastEventList,
                                       Router, N, Downstream>(
        std::move(router), std::move(downstreams));
}

/**
 * \brief Create a processor that routes events to different downstreams.
 *
 * \ingroup processors-branching
 *
 * This processor forwards each event in \p RoutedEventList to a different
 * downstream according to the provided \p router (see \ref routers), which
 * maps events to downstream indices.
 *
 * Events mapped to indices out of range are discarded.
 *
 * All other events (which must be in \p BroadcastEventList) are broadcast to
 * all downstreams.
 *
 * Every downstream must handle all events in \p RoutedEventList and \p
 * BroadcastEventList.
 *
 * Downstreams of identical type are stored in an array and selected by index.
 * Otherwise, each routed event is dispatched with a single indirect call.
 * Downstreams can be wrapped in a common `tcspc::type_erased_processor` to
 * make them identical in type (for example, to reduce type complexity).
 *
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
    requires(is_processor_of_list_v<
                 Downstreams,
                 type_list_union_t<RoutedEventList, BroadcastEventList>> &&
             ...)
auto route(Router router, Downstreams... downstreams) {
    if constexpr (sizeof...(Downstreams) > 0 &&
                  internal::all_same<Downstreams...>::value) {
        return route<RoutedEventList, BroadcastEventList>(
            std::move(router), std::array{std::move(downstreams)...});
    } else {
        static_assert(
            internal::route_event_lists_disjoint_v<RoutedEventList,
                                                   BroadcastEventList>,
            "routed event list and broadcast event list must not overlap");
        return internal::route_heterogeneous<
            RoutedEventList, BroadcastEventList, Router, Downstreams...>(
            std::move(router), std::move(downstreams)...);
    }
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
 * \tparam NumericTraits numeric traits specifying `channel_type`
 */
template <std::size_t N, typename NumericTraits = default_numeric_traits>
class channel_router {
    std::array<typename NumericTraits::channel_type, N> channels;
    std::array<std::size_t, N> indices;

  public:
    /**
     * \brief Construct with channels and corresponding downstream indices.
     *
     * \tparam ChannelIndexPair tuple-like type (`NumericTraits::channel_type`,
     * `std::size_t`) of \p channel_indices (usually deduced)
     *
     * \param channel_indices pairs of channels with downstream indices to
     * route to
     */
    template <typename ChannelIndexPair>
    explicit channel_router(
        std::array<ChannelIndexPair, N> const &channel_indices)
        : channels(std::invoke([&] {
              std::array<typename NumericTraits::channel_type, N> ret{};
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
                                  typename NumericTraits::channel_type> &&
                std::is_convertible_v<
                    decltype(std::get<1>(channel_indices[0])), std::size_t>,
            "channel_indices must be an array of pair-like convertible to (channel, std::size_t)");
    }

    /** \brief Implements router requirement. */
    template <typename Event>
    auto operator()(Event const &event) const -> std::size_t {
        static_assert(std::is_same_v<decltype(event.channel),
                                     typename NumericTraits::channel_type>);
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
 * Every downstream must handle all events in \p BroadcastEventList.
 *
 * This overload is equivalent to the variadic overload given downstreams of
 * identical type.
 *
 * \see `tcspc::route()`
 *
 * \tparam BroadcastEventList event types to handle
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
 * - Types in `BroadcastEventList`: broadcast to every downstream
 * - Flush: broadcast to every downstream
 */
template <typename BroadcastEventList, std::size_t N, typename Downstream>
    requires is_processor_of_list_v<Downstream, BroadcastEventList>
auto broadcast(std::array<Downstream, N> downstreams) {
    return route<type_list<>, BroadcastEventList>(null_router(),
                                                  std::move(downstreams));
}

/**
 * \brief Create a processor that broadcasts events to multiple downstream
 * processors.
 *
 * \ingroup processors-branching
 *
 * Every downstream must handle all events in \p BroadcastEventList.
 *
 * Downstreams of identical type are stored in an array; otherwise in a tuple.
 * Downstreams can be wrapped in a common `tcspc::type_erased_processor` to
 * make them identical in type (for example, to reduce type complexity).
 *
 * \see `tcspc::route()`
 *
 * \tparam BroadcastEventList event types to handle
 *
 * \tparam Downstreams downstream processor types (usually deduced)
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
    requires(is_processor_of_list_v<Downstreams, BroadcastEventList> && ...)
auto broadcast(Downstreams... downstreams) {
    return route<type_list<>, BroadcastEventList>(null_router(),
                                                  std::move(downstreams)...);
}

} // namespace tcspc
