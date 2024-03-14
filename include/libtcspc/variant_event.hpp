/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "type_list.hpp"

#include <ostream>
#include <utility>
#include <variant>

namespace tcspc {

/**
 * \brief A variant type for events.
 *
 * \ingroup events-variant
 *
 * Derives from \c std::variant of the given events. This can be used to store
 * more than one kind of event, for example when buffering.
 *
 * In addition to the \c std::variant members, stream insertion (\c operator<<)
 * is supported (for convenience during testing).
 *
 * \tparam EventList \c type_list specialization describing the events that the
 * variant can hold (must not contain duplicates)
 *
 * \see variant_or_single_event
 */
template <typename EventList> class variant_event;

/** \cond implementation-detail */

template <typename... Events>
class variant_event<type_list<Events...>> : public std::variant<Events...> {
    static_assert(sizeof...(Events) > 0,
                  "variant_event cannot have empty EventList");

    using base_type = std::variant<Events...>;
    using base_type::base_type;

    friend auto operator<<(std::ostream &stream, variant_event const &event)
        -> std::ostream & {
        return std::visit(
            [&](auto const &e) -> std::ostream & { return stream << e; },
            event);
    }
};

/** \endcond */

namespace internal {

template <typename EventList>
struct variant_or_single_event_impl : type_identity<variant_event<EventList>> {
};

template <typename Event>
struct variant_or_single_event_impl<type_list<Event>> : type_identity<Event> {
};

} // namespace internal

/**
 * \brief Sepect the plain event if single, or \c variant_event otherwise.
 *
 * \ingroup events-variant
 *
 * When \p EventList contains a single event type, resolves to
 * that event. Otherwise resolves to \c variant_event<EventList>.
 *
 * \see visit_variant_or_single_event
 */
template <typename EventList>
using variant_or_single_event =
    typename internal::variant_or_single_event_impl<
        unique_type_list_t<EventList>>::type;

/**
 * \brief Apply a visitor to an event that is not a \c variant_event.
 *
 * \ingroup events-variant
 *
 * Simply calls \p visitor, forwarding \p event as its single argument.
 *
 * This is intended for use with event types defined via \c
 * variant_or_single_event.
 */
template <typename Visitor, typename Event>
constexpr auto visit_variant_or_single_event(Visitor visitor, Event &&event) {
    return visitor(std::forward<Event>(event));
}

/**
 * \brief Apply a visitor to a \c variant_event (rvalue).
 *
 * \ingroup events-variant
 *
 * Calls \c std::visit, forwarding \p visitor and \p event as arguments.
 *
 * This is intended for use with event types defined via \c
 * variant_or_single_event.
 */
template <typename Visitor, typename EventList>
constexpr auto
visit_variant_or_single_event(Visitor &&visitor,
                              variant_event<EventList> &&event) {
    return std::visit(std::forward<Visitor>(visitor), std::move(event));
}

/**
 * \brief Apply a visitor to a \c variant_event (const lvalue).
 *
 * \ingroup events-variant
 *
 * Calls \c std::visit, forwarding \p visitor and \p event as arguments.
 *
 * This is intended for use with event types defined via \c
 * variant_or_single_event.
 */
template <typename Visitor, typename EventList>
constexpr auto
visit_variant_or_single_event(Visitor &&visitor,
                              variant_event<EventList> const &event) {
    return std::visit(std::forward<Visitor>(visitor), event);
}

/**
 * \brief Apply a visitor to a \c variant_event (lvalue).
 *
 * \ingroup events-variant
 *
 * Calls \c std::visit, forwarding \p visitor and \p event as arguments.
 *
 * This is intended for use with event types defined via \c
 * variant_or_single_event.
 */
template <typename Visitor, typename EventList>
constexpr auto visit_variant_or_single_event(Visitor &&visitor,
                                             variant_event<EventList> &event) {
    return std::visit(std::forward<Visitor>(visitor), event);
}

} // namespace tcspc
