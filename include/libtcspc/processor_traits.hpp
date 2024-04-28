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
 * \hideinheritancegraph
 *
 * Determines whether the processor \p Proc handles `flush()` (which all
 * processors should) and provides the result in the `bool` member `value`.
 *
 * If the `flush()` exists but has a return type other than `void`, compilation
 * fails.
 *
 * \see `tcspc::handles_flush_v`
 */
template <typename Proc, typename = void>
struct handles_flush : std::false_type {};

/** \cond implementation-detail */

template <typename Proc>
struct handles_flush<Proc, std::void_t<decltype(std::declval<Proc>().flush())>>
    : std::true_type {
    static_assert(std::is_same_v<void, decltype(std::declval<Proc>().flush())>,
                  "flush() must return void");
};

/** \endcond */

/**
 * \brief Helper variable template for `tcspc::handles_flush`.
 */
template <typename Proc>
inline constexpr bool handles_flush_v = handles_flush<Proc>::value;

/** @} <!-- group handles-flush --> */

/**
 * \defgroup handles-rvalue-event Metafunction handles_rvalue_event
 * \ingroup processor-traits
 * \copydoc handles_rvalue_event
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles an event type by
 * rvalue reference.
 *
 * \hideinheritancegraph
 *
 * Determines whether the processor \p Proc handles the event type \p Event
 * passed as rvalue reference and provides the result in the `bool` member
 * `value`.
 *
 * Note that the result is true even if \p Proc only implements `handle(Event
 * const &)`, because rvalues can bind to const lvalue reference. This check is
 * still useful in case the processor has different overloads for rvalue and
 * const lvalue references.
 *
 * If the event handler exists but has a return type other than `void`,
 * compilation fails.
 *
 * \note Even if this metafunction is true, it is possible for a processor to
 * fail (at compile time) to handle the event due to `static_assert` or other
 * implicit requirements.
 *
 * \see `tcspc::handles_rvalue_event_v`
 */
template <typename Proc, typename Evnet, typename = void>
struct handles_rvalue_event : std::false_type {};

/** \cond implementation-detail */

template <typename Proc, typename Event>
struct handles_rvalue_event<Proc, Event,
                            std::void_t<decltype(std::declval<Proc>().handle(
                                std::declval<Event &&>()))>> : std::true_type {
    static_assert(std::is_same_v<void, decltype(std::declval<Proc>().handle(
                                           std::declval<Event &&>()))>,
                  "handle() must return void");
};

/** \endcond */

/**
 * \brief Helper variable template for `tcspc::handles_rvalue_event`.
 */
template <typename Proc, typename Event>
inline constexpr bool handles_rvalue_event_v =
    handles_rvalue_event<Proc, Event>::value;

/** @} <!-- group handles-rvalue-event --> */

/**
 * \defgroup handles-const-event Metafunction handles_const_event
 * \ingroup processor-traits
 * \copydoc handles_const_event
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles an event type by
 * const lvalue reference.
 *
 * \hideinheritancegraph
 *
 * Determines whether the processor \p Proc handles the event type \p Event
 * passed as const lvalue reference and provides the result in the `bool`
 * member `value`.
 *
 * If the event handler exists but has a return type other than `void`,
 * compilation fails.
 *
 * \note Even if this metafunction is true, it is possible for a processor to
 * fail (at compile time) to handle the event due to `static_assert` or other
 * implicit requirements.
 *
 * \see `tcspc::handles_const_event_v`
 */
template <typename Proc, typename Evnet, typename = void>
struct handles_const_event : std::false_type {};

/** \cond implementation-detail */

template <typename Proc, typename Event>
struct handles_const_event<Proc, Event,
                           std::void_t<decltype(std::declval<Proc>().handle(
                               std::declval<Event const &>()))>>
    : std::true_type {
    static_assert(std::is_same_v<void, decltype(std::declval<Proc>().handle(
                                           std::declval<Event const &>()))>,
                  "handle() must return void");
};

/** \endcond */

/**
 * \brief Helper variable template for `tcspc::handles_const_event`.
 */
template <typename Proc, typename Event>
inline constexpr bool handles_const_event_v =
    handles_const_event<Proc, Event>::value;

/** @} <!-- group handles-const-event --> */

/**
 * \defgroup handles-event Metafunction handles_event
 * \ingroup processor-traits
 * \copydoc handles_event
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles an event type.
 *
 * \hideinheritancegraph
 *
 * Determines whether the processor \p Proc handles the event type \p Event and
 * provides the result in the `bool` member `value`.
 *
 * This is equivalent to the logical AND of `tcspc::handles_const_event<Proc,
 * Event>` and `tcspc::handles_rvalue_event<Proc, Event>`.
 *
 * If the event handler(s) exist but have a return type other than `void`,
 * compilation fails.
 *
 * \note Even if this metafunction is true, it is possible for a processor to
 * fail (at compile time) to handle the event due to `static_assert` or other
 * implicit requirements.
 *
 * \see `tcspc::handles_event_v`
 */
template <typename Proc, typename Event>
struct handles_event : std::conjunction<handles_const_event<Proc, Event>,
                                        handles_rvalue_event<Proc, Event>> {};

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
 * If the event handler(s) exist but have a return type other than `void`,
 * compilation fails.
 *
 * \note Even if this metafunction is true, it is possible for a processor to
 * fail (at compile time) to handle the events due to `static_assert` or other
 * implicit requirements.
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
