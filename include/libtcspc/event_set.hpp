/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "apply_class_template.hpp"

#include <exception>
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
 * This is just an alias for a std::variant holding the events of the given
 * event set. It can be used to store more than one kind of event, for example
 * for buffering.
 *
 * \tparam EventSet the event set type describing the events that the variant
 * can hold
 */
template <typename EventSet>
class event_variant
    : public internal::apply_class_template_t<std::variant, EventSet> {
    using base_type = internal::apply_class_template_t<std::variant, EventSet>;
    using base_type::base_type;

    // Note: event_variant needs to derive from std::variant, rather than be an
    // alias template, because we need to befriend the stream insertion
    // operator.

    /** \brief Stream insertion operator */
    friend auto operator<<(std::ostream &stream, event_variant const &event)
        -> std::ostream & {
        return std::visit(
            [&](auto const &e) -> std::ostream & { return stream << e; },
            event);
    }
};

namespace internal {

template <typename Event, typename... Events>
struct event_is_one_of : std::disjunction<std::is_same<Event, Events>...> {};

} // namespace internal

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
template <typename EventSet, typename Event>
struct contains_event
    : internal::apply_class_template_t<internal::event_is_one_of, EventSet,
                                       Event> {};

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
                     std::void_t<decltype(std::declval<Proc>().handle_event(
                         std::declval<Event const>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().handle_event(
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
 * \brief Metafunction to check whether the given processor handles end of
 * stream.
 *
 * \ingroup metafunctions
 *
 * All processors must handle end of stream. This metafunction is useful for
 * static checking.
 *
 * The result is provided in the member constant \c value.
 *
 * \see handles_end_v
 *
 * \tparam Proc a processor type to check
 */
template <typename Proc, typename = void>
struct handles_end : std::false_type {};

template <typename Proc>
struct handles_end<Proc, std::void_t<decltype(std::declval<Proc>().handle_end(
                             std::declval<std::exception_ptr const>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().handle_end(
                             std::declval<std::exception_ptr const>()))> {};

/**
 * \brief Helper variable to get the result of handles_end.
 *
 * \ingroup metafunctions
 *
 * \tparam Proc a processor type to check
 */
template <typename Proc>
inline constexpr bool handles_end_v = handles_end<Proc>::value;

namespace internal {

template <typename Proc, typename... Events>
struct handles_events : std::conjunction<handles_event<Proc, Events>...> {};

template <typename Proc, typename... Events>
struct handles_events_and_end
    : std::conjunction<handles_events<Proc, Events...>, handles_end<Proc>> {};

} // namespace internal

/**
 * \brief Metafunction to check whether the given processor handles a set of
 * events.
 *
 * \ingroup metafunctions
 *
 * The result is true only if the processor handles all of the events in the
 * event set as well as end-of-stream.
 *
 * The result is provided in the member constant \c value.
 *
 * \tparam Proc the processor type to check
 *
 * \tparam EventSet the event set to check
 */
template <typename Proc, typename EventSet>
struct handles_event_set
    : internal::apply_class_template_t<internal::handles_events_and_end,
                                       EventSet, Proc> {};

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
