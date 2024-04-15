/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "type_list.hpp"

#include <type_traits>

namespace tcspc {

/**
 * \defgroup handles-flush Metafunction handles_flush
 * \ingroup processor-traits
 * \copydoc handles_flush
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles flushing.
 *
 * Determines whether the processor \p Proc handles `flush()` (which all
 * processors should) and provides the result in the `bool` member `value`.
 *
 * \see `tcspc::handles_flush_v`
 */
template <typename Proc, typename = void>
struct handles_flush : std::false_type {};

/** \cond implementation-detail */

template <typename Proc>
struct handles_flush<Proc, std::void_t<decltype(std::declval<Proc>().flush())>>
    : std::is_same<void, decltype(std::declval<Proc>().flush())> {};

/** \endcond */

/**
 * \brief Helper variable template for `tcspc::handles_flush`.
 */
template <typename Proc>
inline constexpr bool handles_flush_v = handles_flush<Proc>::value;

/** @} <!-- group handles-flush --> */

/**
 * \defgroup handles-event Metafunction handles_event
 * \ingroup processor-traits
 * \copydoc handles_event
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles an event type.
 *
 * Determines whether the processor \p Proc handles the event type \p Event and
 * provides the result in the `bool` member `value`.
 *
 * \see `tcspc::handles_event_v`
 */
template <typename Proc, typename Event, typename = void>
struct handles_event : std::false_type {};

/** \cond implementation-detail */

template <typename Proc, typename Event>
struct handles_event<Proc, Event,
                     std::void_t<decltype(std::declval<Proc>().handle(
                         std::declval<Event const>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().handle(
                             std::declval<Event const>()))> {};

/** \endcond */

/**
 * \brief Helper variable template for `tcspc::handles_event`.
 */
template <typename Proc, typename Event>
inline constexpr bool handles_event_v = handles_event<Proc, Event>::value;

/** @} <!-- group handles-event --> */

/**
 * \defgroup handles-events Metafunction handles_events
 * \ingroup processor-traits
 * \copydoc handles_events
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles a set of event
 * types.
 *
 * Determines whether the processor \p Proc handles all of the event types in
 * the `tcspc::type_list` specialization \p EventList and provides the result
 * in the `bool` member `value`.
 *
 * \see `tcspc::handles_events_v`
 */
template <typename Proc, typename EventList> struct handles_events;

/** \cond implementation-detail */

template <typename Proc, typename... Events>
struct handles_events<Proc, type_list<Events...>>
    : std::conjunction<handles_event<Proc, Events>...> {};

/** \endcond */

/**
 * \brief Helper variable template for `tcspc::handles_events`.
 */
template <typename Proc, typename EventList>
inline constexpr bool handles_events_v =
    handles_events<Proc, EventList>::value;

/** @} <!-- group handles-events --> */

} // namespace tcspc
