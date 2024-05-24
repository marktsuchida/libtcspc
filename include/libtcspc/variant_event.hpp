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
 * \defgroup events-variant Variant event types
 * \ingroup events-core
 *
 * \brief Event types representing a type-safe union of event types.
 *
 * Variant events can be used to treat a number of event types as a single
 * type, for example to allow buffering of a stream of more than one type of
 * event.
 *
 * The type `tcspc::variant_event` is a thin wrapper around `std::variant`. In
 * generic code working with an unknown set of event types, the type alias
 * `tcspc::variant_or_single_event` is useful to avoid the overhead of
 * `std::variant` when there is only one event type.
 *
 * An equivalent to `std::visit` for use with `tcspc::variant_or_single_event`
 * is provided: `tcspc::visit_variant_or_single_event()`.
 *
 * @{
 */

/**
 * \brief Variant event.
 *
 * \ingroup events-variant
 *
 * Publicly derives from `std::variant<Es...>`, where `EventList` is
 * `tcspc::type_list<Es...>`.
 *
 * In addition to `std::variant` members, the stream insertion (`operator<<`)
 * is supported (for convenience during testing).
 *
 * \tparam EventList A `tcspc::type_list` specialization containing the event
 * types that the variant event can hold (must not contain duplicates)
 *
 * \see `tcspc::variant_or_single_event`
 * \see `tcspc::multiplex()`
 * \see `tcspc::demultiplex()`
 */
template <typename EventList> class variant_event;

/** \cond implementation-detail */

template <typename... Events>
class variant_event<type_list<Events...>> : public std::variant<Events...> {
    static_assert(sizeof...(Events) > 0,
                  "variant_event cannot have empty EventList");

    using base_type = std::variant<Events...>;
    using base_type::base_type;

    friend auto operator<<(std::ostream &stream,
                           variant_event const &event) -> std::ostream & {
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
 * \brief Select the plain event if single, otherwise `tcspc::variant_event`.
 *
 * \ingroup events-variant
 *
 * When \p EventList contains a single event type, resolves to
 * that event. Otherwise resolves to `tcspc::variant_event<EventList>`.
 *
 * \see \ref visit-variant-or-single-event
 */
template <typename EventList>
using variant_or_single_event =
    typename internal::variant_or_single_event_impl<
        unique_type_list_t<EventList>>::type;

/**
 * \defgroup visit-variant-or-single-event Visiting a variant-or-single event
 *
 * \brief Equivalent of `std::visit` for `tcspc::variant_or_single_event`.
 *
 * This overload set of functions together take any
 * `tcspc::variant_or_single_event` as the second parameter.
 *
 * (Currently there are only overloads for a single event argument, with no
 * variadic version.)
 *
 * @{
 */

/**
 * \brief Apply a visitor to an event that is not a `tcspc::variant_event`.
 *
 * Calls \p visitor, forwarding \p event as its single argument.
 */
template <typename Visitor, typename Event>
constexpr auto visit_variant_or_single_event(Visitor visitor, Event &&event) {
    return visitor(std::forward<Event>(event));
}

/**
 * \brief Apply a visitor to a `tcspc::variant_event` (rvalue).
 *
 * Calls `std::visit`, forwarding \p visitor and \p event as arguments.
 */
template <typename Visitor, typename EventList>
constexpr auto
visit_variant_or_single_event(Visitor &&visitor,
                              variant_event<EventList> &&event) {
    return std::visit(std::forward<Visitor>(visitor), std::move(event));
}

/**
 * \brief Apply a visitor to a `tcspc::variant_event` (const lvalue).
 *
 * Calls `std::visit`, forwarding \p visitor and \p event as arguments.
 */
template <typename Visitor, typename EventList>
constexpr auto
visit_variant_or_single_event(Visitor &&visitor,
                              variant_event<EventList> const &event) {
    return std::visit(std::forward<Visitor>(visitor), event);
}

/**
 * \brief Apply a visitor to a `tcspc::variant_event` (lvalue).
 *
 * Calls `std::visit`, forwarding \p visitor and \p event as arguments.
 */
template <typename Visitor, typename EventList>
constexpr auto visit_variant_or_single_event(Visitor &&visitor,
                                             variant_event<EventList> &event) {
    return std::visit(std::forward<Visitor>(visitor), event);
}

/** @} <!-- group visit-variant-or-single-event --> */

/** @} <!-- group events-variant --> */

} // namespace tcspc
