/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <ostream>
#include <tuple>
#include <type_traits>
#include <variant>

namespace tcspc {

/**
 * \brief Type representing a set of event types.
 *
 * \ingroup misc
 *
 * This is just an alias for std::tuple. It is used to describe sets of events
 * handled or emitted by processors.
 *
 * \tparam Events the event types
 */
template <typename... Events> using event_set = std::tuple<Events...>;

/**
 * \brief A variant type for events.
 *
 * \ingroup misc
 *
 * Derives from \c std::variant of the events in the given event set. This can
 * be used to store more than one kind of event, for example when buffering.
 *
 * Stream insertion (\c operator<<) is supported.
 *
 * \tparam EventSet the event set type describing the events that the variant
 * can hold
 */
template <typename EventSet> class event_variant;

/**
 * \brief Implementation detail of \c event_variant.
 *
 * \ingroup misc
 */
template <typename... Events>
class event_variant<event_set<Events...>> : public std::variant<Events...> {
    using base_type = std::variant<Events...>;
    using base_type::base_type;

    /** \brief Stream insertion operator */
    friend auto operator<<(std::ostream &stream, event_variant const &event)
        -> std::ostream & {
        return std::visit(
            [&](auto const &e) -> std::ostream & { return stream << e; },
            event);
    }
};

/**
 * \brief Metafunction to get the number of events in an event set.
 *
 * \ingroup metafunctions
 *
 * Duplicate events are counted twice.
 *
 * \see event_set_size_v
 *
 * \tparam EventSet an event set
 */
template <typename EventSet> using event_set_size = std::tuple_size<EventSet>;

/**
 * \brief Helper variable to get the result of event_set_size
 *
 * \ingroup metafunctions
 *
 * \tparam EventSet an event set
 */
template <typename EventSet>
inline constexpr std::size_t event_set_size_v =
    event_set_size<EventSet>::value;

/**
 * \brief Metafunction to get the type of element of an event set.
 *
 * \ingroup metafunctions
 *
 * Because order is (semantically) not significant in \c event_set, this
 * metafunction is mostly useful for obtaining element 0 of a size-1 event set.
 *
 * \see event_set_element_t
 *
 * \tparam I the index of the element
 *
 * \tparam EventSet an event set
 */
template <std::size_t I, typename EventSet>
using event_set_element = std::tuple_element<I, EventSet>;

/**
 * \brief Helper typedef to get the result of event_set_element.
 *
 * \ingroup metafunctions
 *
 * \tparam I the index of the element
 *
 * \tparam EventSet an event set
 */
template <std::size_t I, typename EventSet>
using event_set_element_t = typename event_set_element<I, EventSet>::type;

/**
 * \brief Metafunction to check whether the given event set contains the given
 * event.
 *
 * \ingroup metafunctions
 *
 * The result is provided in the member constant \c value.
 *
 * \see contains_event_v
 *
 * \tparam EventSet an event set type to check
 *
 * \tparam Event an event type to check
 */
template <typename EventSet, typename Event> struct contains_event;

template <typename Event, typename... Events>
struct contains_event<event_set<Events...>, Event>
    : std::disjunction<std::is_same<Events, Event>...> {};

/**
 * \brief Helper variable to get the result of contains_event
 *
 * \ingroup metafunctions
 *
 * \tparam EventSet an event set type to check
 *
 * \tparam Event an event type to check
 */
template <typename EventSet, typename Event>
inline constexpr bool contains_event_v =
    contains_event<EventSet, Event>::value;

/**
 * \brief Metafunction to check whether the given processor handles the given
 * event type.
 *
 * \ingroup metafunctions
 *
 * The result is provided in the member constant \c value.
 *
 * \see handles_event_v
 *
 * \tparam Proc a processor type to check
 *
 * \tparam Event an event type to check
 */
template <typename Proc, typename Event, typename = void>
struct handles_event : std::false_type {};

template <typename Proc, typename Event>
struct handles_event<Proc, Event,
                     std::void_t<decltype(std::declval<Proc>().handle(
                         std::declval<Event const>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().handle(
                             std::declval<Event const>()))> {};

/**
 * \brief Helper variable to get the result of handles_event.
 *
 * \ingroup metafunctions
 *
 * \tparam Proc a processor type to check
 *
 * \tparam Event an event type to check
 */
template <typename Proc, typename Event>
inline constexpr bool handles_event_v = handles_event<Proc, Event>::value;

/**
 * \brief Metafunction to check whether the given processor handles flushing.
 *
 * \ingroup metafunctions
 *
 * All processors must handle flushing. This metafunction is useful for static
 * checking.
 *
 * The result is provided in the member constant \c value.
 *
 * \see handles_flush_v
 *
 * \tparam Proc a processor type to check
 */
template <typename Proc, typename = void>
struct handles_flush : std::false_type {};

template <typename Proc>
struct handles_flush<Proc, std::void_t<decltype(std::declval<Proc>().flush())>>
    : std::is_same<void, decltype(std::declval<Proc>().flush())> {};

/**
 * \brief Helper variable to get the result of handles_flush.
 *
 * \ingroup metafunctions
 *
 * \tparam Proc a processor type to check
 */
template <typename Proc>
inline constexpr bool handles_flush_v = handles_flush<Proc>::value;

namespace internal {

template <typename Proc, typename... Events>
struct handles_events : std::conjunction<handles_event<Proc, Events>...> {};

template <typename Proc, typename... Events>
struct handles_events_and_flush
    : std::conjunction<handles_events<Proc, Events...>, handles_flush<Proc>> {
};

} // namespace internal

/**
 * \brief Metafunction to check whether the given processor handles a set of
 * events.
 *
 * \ingroup metafunctions
 *
 * The result is true only if the processor handles all of the events in the
 * event set as well as flush.
 *
 * The result is provided in the member constant \c value.
 *
 * \tparam Proc the processor type to check
 *
 * \tparam EventSet the event set to check
 */
template <typename Proc, typename EventSet> struct handles_event_set;

template <typename Proc, typename... Events>
struct handles_event_set<Proc, event_set<Events...>>
    : internal::handles_events_and_flush<Proc, Events...> {};

/**
 * \brief Helper variable to get the result of handles_event_set.
 *
 * \ingroup metafunctions
 *
 * \tparam Proc the processor type to check
 *
 * \tparam EventSet the event set to check
 */
template <typename Proc, typename EventSet>
inline constexpr bool handles_event_set_v =
    handles_event_set<Proc, EventSet>::value;

/**
 * \brief Metafunction to concatenate event sets
 *
 * \ingroup metafunctions
 *
 * The result is a new event set type that contains all of the events of the
 * given event sets. No attempt is made to remove duplicate event types.
 *
 * The result is provided in the member type \c type.
 *
 * \tparam EventSets the event set types to concatenate
 */
template <typename... EventSets> struct concat_event_set {
    /**
     * \brief Returned type of the metafunction.
     */
    using type = decltype(std::tuple_cat(std::declval<EventSets>()...));
};

/**
 * \brief Helper typedef to get the result of concat_event_set.
 *
 * \ingroup metafunctions
 *
 * \tparam EventSets the event set types to concatenate
 */
template <typename... EventSets>
using concat_event_set_t = typename concat_event_set<EventSets...>::type;

} // namespace tcspc
