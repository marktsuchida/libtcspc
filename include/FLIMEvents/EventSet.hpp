/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ApplyClassTemplateToTupleElements.hpp"

#include <exception>
#include <tuple>
#include <type_traits>
#include <variant>

namespace flimevt {

/**
 * \brief Type representing a set of event types.
 *
 * This is just an alias for std::tuple. It is used to describe sets of events
 * handled or emitted by processors.
 *
 * \tparam Events the event types
 */
template <typename... Events> using EventSet = std::tuple<Events...>;

/**
 * \brief A variant type for events.
 *
 * This is just an alias for a std::variant holding the events of the given
 * event set. It can be used to store more than one kind of event, for example
 * for buffering.
 *
 * \tparam ESet the event set type describing the events that the variant can
 * hold
 */
template <typename ESet>
using EventVariant = ApplyClassTemplateToTupleElementsT<std::variant, ESet>;

namespace internal {

template <typename Event, typename... Events>
struct EventIsOneOf : std::disjunction<std::is_same<Event, Events>...> {};

} // namespace internal

/**
 * \brief Metafunction to check whether the given event set contains the given
 * event.
 *
 * The result is provided in the member constant \c value.
 *
 * \see ContainsEventV
 *
 * \tparam ESet an event set type to check
 * \tparam Event an event type to check
 */
template <typename ESet, typename Event>
struct ContainsEvent
    : ApplyClassTemplateToTupleElementsT<internal::EventIsOneOf, ESet, Event> {
};

/**
 * \brief Helper variable to get the result of ContainsEvent
 *
 * \tparam ESet an event set type to check
 * \tparam Event an event type to check
 */
template <typename ESet, typename Event>
inline constexpr bool ContainsEventV = ContainsEvent<ESet, Event>::value;

/**
 * \brief Metafunction to check whether the given processor handles the given
 * event type.
 *
 * The result is provided in the member constant \c value.
 *
 * \see HandlesEventV
 *
 * \tparam Proc a processor type to check
 * \tparam Event an event type to check
 */
template <typename Proc, typename Event, typename = void>
struct HandlesEvent : std::false_type {};

template <typename Proc, typename Event>
struct HandlesEvent<Proc, Event,
                    std::void_t<decltype(std::declval<Proc>().HandleEvent(
                        std::declval<Event>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().HandleEvent(
                             std::declval<Event>()))> {};

/**
 * \brief Helper variable to get the result of HandlesEvent.
 *
 * \tparam Proc a processor type to check
 * \tparam Event an event type to check
 */
template <typename Proc, typename Event>
inline constexpr bool HandlesEventV = HandlesEvent<Proc, Event>::value;

/**
 * \brief Metafunction to check whether the given processor handles end of
 * stream.
 *
 * All processors must handle end of stream. This metafunction is useful for
 * static checking.
 *
 * The result is provided in the member constant \c value.
 *
 * \see HandlesEndV
 *
 * \tparam Proc a processor type to check
 */
template <typename Proc, typename = void>
struct HandlesEnd : std::false_type {};

template <typename Proc>
struct HandlesEnd<Proc, std::void_t<decltype(std::declval<Proc>().HandleEnd(
                            std::declval<std::exception_ptr>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().HandleEnd(
                             std::declval<std::exception_ptr>()))> {};

/**
 * \brief Helper variable to get the result of HandlesEnd.
 *
 * \tparam Proc a processor type to check
 */
template <typename Proc>
inline constexpr bool HandlesEndV = HandlesEnd<Proc>::value;

namespace internal {

template <typename Proc, typename... Events>
struct HandlesEvents : std::conjunction<HandlesEvent<Proc, Events>...> {};

template <typename Proc, typename... Events>
struct HandlesEventsAndEnd
    : std::conjunction<HandlesEvents<Proc, Events...>, HandlesEnd<Proc>> {};

} // namespace internal

/**
 * \brief Metafunction to check whether the given processor handles a set of
 * events.
 *
 * The result is true only if the processor handles all of the events in the
 * event set as well as end-of-stream.
 *
 * The result is provided in the member constant \c value.
 *
 * \tparam Proc the processor type to check
 * \tparam ESet the event set to check
 */
template <typename Proc, typename ESet>
struct HandlesEventSet
    : ApplyClassTemplateToTupleElementsT<internal::HandlesEventsAndEnd, ESet,
                                         Proc> {};

/**
 * \brief Helper variable to get the result of HandlesEventSet.
 *
 * \tparam Proc the processor type to check
 * \tparam ESet the event set to check
 */
template <typename Proc, typename ESet>
inline constexpr bool HandlesEventSetV = HandlesEventSet<Proc, ESet>::value;

/**
 * \brief Metafunction to concatenate event sets
 *
 * The result is a new event set type that contains all of the events of the
 * given event sets. No attempt is made to remove duplicate event types.
 *
 * The result is provided in the member type \c type.
 *
 * \tparam ESets the event set types to concatenate
 */
template <typename... ESets> struct ConcatEventSet {
    /**
     * \brief Returned type of the metafunction.
     */
    using type = decltype(std::tuple_cat(std::declval<ESets>()...));
};

/**
 * \brief Helper typedef to get the result of ConcatEventSet.
 *
 * \tparam ESets the event set types to concatenate
 */
template <typename... ESets>
using ConcatEventSetT = typename ConcatEventSet<ESets...>::type;

} // namespace flimevt
