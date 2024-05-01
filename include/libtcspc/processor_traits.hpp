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
 * \note A true result indicates that \p Proc handles `flush()` _provided that
 * `Proc` and `Proc::flush()` can be instantiated_ (if \p Proc is a template
 * class). It is possible that instantiation will fail (due to `static_assert`
 * failures or other issues) even if the result of this metafunction is true.
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
 * This rvalue-specific check is mostly useful when debugging SFINAE-based
 * constraints on the `handle()` overloads of a processor.
 *
 * Note that the result is true even if \p Proc only implements `handle(Event
 * const &)`, because rvalues can bind to a const lvalue reference. This check
 * is still useful in case the processor has different overloads for rvalue and
 * const lvalue references.
 *
 * If the event handler exists but has a return type other than `void`,
 * compilation fails.
 *
 * \note A true result indicates that \p Proc handles `Event &&` _provided that
 * `Proc` and the relevant `Proc::handle()` overload can be instantiated_ (if
 * \p Proc is a template class). It is possible that instantiation will fail
 * (due to `static_assert` failures or other issues) even if the result of this
 * metafunction is true.
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
 * This const-specific check is mostly useful when debugging SFINAE-based
 * constraints on the `handle()` overloads of a processor.
 *
 * If the event handler exists but has a return type other than `void`,
 * compilation fails.
 *
 * \note A true result indicates that \p Proc handles `Event const &` _provided
 * that `Proc` and the relevant `Proc::handle()` overload can be instantiated_
 * (if \p Proc is a template class). It is possible that instantiation will
 * fail (due to `static_assert` failures or other issues) even if the result of
 * this metafunction is true.
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
 * \note A true result indicates that \p Proc handles `Event` _provided that
 * `Proc` and the relevant `Proc::handle()` overload(s) can be instantiated_
 * (if \p Proc is a template class). It is possible that instantiation will
 * fail (due to `static_assert` failures or other issues) even if the result of
 * this metafunction is true.
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
 * \brief Metafunction to check whether a processor handles the given event
 * types.
 *
 * \hideinheritancegraph
 *
 * Determines whether the processor \p Proc handles all of the event types \p
 * Events and provides the result in the `bool` member `value`.
 *
 * If the event handler(s) exist but any of them has a return type other than
 * `void`, compilation fails.
 *
 * \note A true result indicates that \p Proc handles the `Events` _provided
 * that `Proc` and the relevant `Proc::handle()` overloads can be instantiated_
 * (if \p Proc is a template class). It is possible that instantiation will
 * fail (due to `static_assert` failures or other issues) even if the result of
 * this metafunction is true.
 *
 * \see `tcspc::handles_events_v`
 */
template <typename Proc, typename... Events>
struct handles_events : std::conjunction<handles_event<Proc, Events>...> {};

/**
 * \brief Helper variable template for `tcspc::handles_events`.
 */
template <typename Proc, typename... Events>
inline constexpr bool handles_events_v =
    handles_events<Proc, Events...>::value;

/** @} <!-- group handles-events --> */

/**
 * \defgroup handles-event-list Metafunction handles_event_list
 * \ingroup processor-traits
 * \copydoc handles_event_list
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles a list of event
 * types.
 *
 * Determines whether the processor \p Proc handles all of the event types in
 * the `tcspc::type_list` specialization \p EventList and provides the result
 * in the `bool` member `value`.
 *
 * If the event handler(s) exist but any of them has a return type other than
 * `void`, compilation fails.
 *
 * \note A true result indicates that \p Proc handles the events in `EventList`
 * _provided that `Proc` and the relevant `Proc::handle()` overloads can be
 * instantiated_ (if \p Proc is a template class). It is possible that
 * instantiation will fail (due to `static_assert` failures or other issues)
 * even if the result of this metafunction is true.
 *
 * \see `tcspc::handles_event_list_v`
 */
template <typename Proc, typename EventList> struct handles_event_list {
    static_assert(is_type_list_v<EventList>);
};

/** \cond implementation-detail */

template <typename Proc, typename... Events>
struct handles_event_list<Proc, type_list<Events...>>
    : std::conjunction<handles_event<Proc, Events>...> {};

/** \endcond */

/**
 * \brief Helper variable template for `tcspc::handles_event_list`.
 */
template <typename Proc, typename EventList>
inline constexpr bool handles_event_list_v =
    handles_event_list<Proc, EventList>::value;

/** @} <!-- group handles-event-list --> */

/**
 * \defgroup is-processor Metafunction is_processor
 * \ingroup processor-traits
 * \copydoc is_processor
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles the given event
 * types and flush.
 *
 * \hideinheritancegraph
 *
 * Determines whether the processor \p Proc handles all of the event types \p
 * Events as well as `flush()` and provides the result in the `bool` member
 * `value`.
 *
 * This is equivalent to the logical AND of `tcspc::handles_events<Proc,
 * Events...>` and `tcspc::handles_flush<Proc>`.
 *
 * If the event handler(s) and/or `flush()` exist but any of them has a return
 * type other than `void`, compilation fails.
 *
 * \note A true result indicates that \p Proc handles flush and the `Events`
 * _provided that `Proc`, `Proc::flush()`, and the relevant `Proc::handle()`
 * overloads can be instantiated_ (if \p Proc is a template class). It is
 * possible that instantiation will fail (due to `static_assert` failures or
 * other issues) even if the result of this metafunction is true.
 *
 * \see `tcspc::is_processor_v`
 */
template <typename Proc, typename... Events>
struct is_processor
    : std::conjunction<handles_flush<Proc>, handles_events<Proc, Events...>> {
};

/**
 * \brief Helper variable template for `tcspc::is_processor`.
 */
template <typename Proc, typename... Events>
inline constexpr bool is_processor_v = is_processor<Proc, Events...>::value;

/** @} <!-- group is-processor --> */

/**
 * \defgroup is-processor-of-list Metafunction is_processor_of_list
 * \ingroup processor-traits
 * \copydoc is_processor_of_list
 * @{
 */

/**
 * \brief Metafunction to check whether a processor handles a list of event
 * types and flush.
 *
 * \hideinheritancegraph
 *
 * Determines whether the processor \p Proc handles all of the event types in
 * the `tcspc::type_list` specialization \p EventList as well as `flush()` and
 * provides the result in the `bool` member `value`.
 *
 * This is equivalent to the logical AND of `tcspc::handles_event_list<Proc,
 * EventList>` and `tcspc::handles_flush<Proc>`.
 *
 * If the event handler(s) and/or `flush()` exist but any of them has a return
 * type other than `void`, compilation fails.
 *
 * \note A true result indicates that \p Proc handles flush and the events in
 * `EventList` _provided that `Proc`, `Proc::flush()`, and the relevant
 * `Proc::handle()` overloads can be instantiated_ (if \p Proc is a template
 * class). It is possible that instantiation will fail (due to `static_assert`
 * failures or other issues) even if the result of this metafunction is true.
 *
 * \see `tcspc::is_processor_of_list_v`
 */
template <typename Proc, typename EventList>
struct is_processor_of_list
    : std::conjunction<handles_flush<Proc>,
                       handles_event_list<Proc, EventList>> {
    static_assert(is_type_list_v<EventList>);
};

/**
 * \brief Helper variable template for `tcspc::is_processor_of_list`.
 */
template <typename Proc, typename EventList>
inline constexpr bool is_processor_of_list_v =
    is_processor_of_list<Proc, EventList>::value;

/** @} <!-- group is-processor-of-list --> */

} // namespace tcspc
