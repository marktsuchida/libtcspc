/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "type_list.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace tcspc {

/**
 * \defgroup handles-flush Concept handles_flush
 * \ingroup processor-traits
 * \copydoc handles_flush
 * @{
 */

/**
 * \brief Concept that is satisfied when a processor handles flushing.
 *
 * Determines whether the processor \p Proc handles `flush()` (which all
 * processors should).
 *
 * If `flush()` exists but has a return type other than `void`, the concept is
 * not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles `flush()` _provided
 * that `Proc` and `Proc::flush()` can be instantiated_ (if \p Proc is a
 * template class). It is possible that instantiation will fail (due to
 * `static_assert` failures or other issues) even if the concept is satisfied.
 *
 * \see `tcspc::handles_flush_v`
 */
template <typename Proc>
concept handles_flush = requires(Proc p) {
    { p.flush() } -> std::same_as<void>;
};

/**
 * \brief Helper variable template for `tcspc::handles_flush`.
 */
template <typename Proc>
inline constexpr bool handles_flush_v = handles_flush<Proc>;

/** @} <!-- group handles-flush --> */

/**
 * \defgroup handles-rvalue-event Concept handles_rvalue_event
 * \ingroup processor-traits
 * \copydoc handles_rvalue_event
 * @{
 */

/**
 * \brief Concept that is satisfied when a processor handles an event type by
 * rvalue reference.
 *
 * Determines whether the processor \p Proc handles the event type \p Event
 * passed as rvalue reference.
 *
 * This rvalue-specific check is mostly useful when debugging concept-based
 * constraints on the `handle()` overloads of a processor.
 *
 * Note that the concept is satisfied even if \p Proc only implements
 * `handle(Event const &)`, because rvalues can bind to a const lvalue
 * reference. This check is still useful in case the processor has different
 * overloads for rvalue and const lvalue references.
 *
 * If the event handler exists but has a return type other than `void`, the
 * concept is not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles `Event &&`
 * _provided that `Proc` and the relevant `Proc::handle()` overload can be
 * instantiated_ (if \p Proc is a template class). It is possible that
 * instantiation will fail (due to `static_assert` failures or other issues)
 * even if the concept is satisfied.
 *
 * \see `tcspc::handles_rvalue_event_v`
 */
template <typename Proc, typename Event>
concept handles_rvalue_event = requires(Proc p, Event &&e) {
    { p.handle(std::move(e)) } -> std::same_as<void>;
};

/**
 * \brief Helper variable template for `tcspc::handles_rvalue_event`.
 */
template <typename Proc, typename Event>
inline constexpr bool handles_rvalue_event_v =
    handles_rvalue_event<Proc, Event>;

/** @} <!-- group handles-rvalue-event --> */

/**
 * \defgroup handles-const-event Concept handles_const_event
 * \ingroup processor-traits
 * \copydoc handles_const_event
 * @{
 */

/**
 * \brief Concept that is satisfied when a processor handles an event type by
 * const lvalue reference.
 *
 * Determines whether the processor \p Proc handles the event type \p Event
 * passed as const lvalue reference.
 *
 * This const-specific check is mostly useful when debugging concept-based
 * constraints on the `handle()` overloads of a processor.
 *
 * If the event handler exists but has a return type other than `void`, the
 * concept is not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles `Event const &`
 * _provided that `Proc` and the relevant `Proc::handle()` overload can be
 * instantiated_ (if \p Proc is a template class). It is possible that
 * instantiation will fail (due to `static_assert` failures or other issues)
 * even if the concept is satisfied.
 *
 * \see `tcspc::handles_const_event_v`
 */
template <typename Proc, typename Event>
concept handles_const_event = requires(Proc p, Event const &e) {
    { p.handle(e) } -> std::same_as<void>;
};

/**
 * \brief Helper variable template for `tcspc::handles_const_event`.
 */
template <typename Proc, typename Event>
inline constexpr bool handles_const_event_v = handles_const_event<Proc, Event>;

/** @} <!-- group handles-const-event --> */

/**
 * \defgroup handles-event Concept handles_event
 * \ingroup processor-traits
 * \copydoc handles_event
 * @{
 */

/**
 * \brief Concept that is satisfied when a processor handles an event type.
 *
 * Determines whether the processor \p Proc handles the event type \p Event.
 *
 * This is equivalent to the logical AND of `tcspc::handles_const_event<Proc,
 * Event>` and `tcspc::handles_rvalue_event<Proc, Event>`.
 *
 * If the event handler(s) exist but have a return type other than `void`, the
 * concept is not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles `Event` _provided
 * that `Proc` and the relevant `Proc::handle()` overload(s) can be
 * instantiated_ (if \p Proc is a template class). It is possible that
 * instantiation will fail (due to `static_assert` failures or other issues)
 * even if the concept is satisfied.
 *
 * \see `tcspc::handles_event_v`
 */
template <typename Proc, typename Event>
concept handles_event =
    handles_const_event<Proc, Event> && handles_rvalue_event<Proc, Event>;

/**
 * \brief Helper variable template for `tcspc::handles_event`.
 */
template <typename Proc, typename Event>
inline constexpr bool handles_event_v = handles_event<Proc, Event>;

/** @} <!-- group handles-event --> */

/**
 * \defgroup handles-events Concept handles_events
 * \ingroup processor-traits
 * \copydoc handles_events
 * @{
 */

/**
 * \brief Concept that is satisfied when a processor handles the given event
 * types.
 *
 * Determines whether the processor \p Proc handles all of the event types \p
 * Events.
 *
 * If the event handler(s) exist but any of them has a return type other than
 * `void`, the concept is not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles the `Events`
 * _provided that `Proc` and the relevant `Proc::handle()` overloads can be
 * instantiated_ (if \p Proc is a template class). It is possible that
 * instantiation will fail (due to `static_assert` failures or other issues)
 * even if the concept is satisfied.
 *
 * \see `tcspc::handles_events_v`
 */
template <typename Proc, typename... Events>
concept handles_events = (handles_event<Proc, Events> && ...);

/**
 * \brief Helper variable template for `tcspc::handles_events`.
 */
template <typename Proc, typename... Events>
inline constexpr bool handles_events_v = handles_events<Proc, Events...>;

/** @} <!-- group handles-events --> */

/**
 * \defgroup is-processor Concept is_processor
 * \ingroup processor-traits
 * \copydoc is_processor
 * @{
 */

/**
 * \brief Concept that is satisfied when a processor handles the given event
 * types and flush.
 *
 * Determines whether the processor \p Proc handles all of the event types \p
 * Events as well as `flush()`.
 *
 * This is equivalent to the logical AND of `tcspc::handles_events<Proc,
 * Events...>` and `tcspc::handles_flush<Proc>`.
 *
 * If the event handler(s) and/or `flush()` exist but any of them has a return
 * type other than `void`, the concept is not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles flush and the
 * `Events` _provided that `Proc`, `Proc::flush()`, and the relevant
 * `Proc::handle()` overloads can be instantiated_ (if \p Proc is a template
 * class). It is possible that instantiation will fail (due to `static_assert`
 * failures or other issues) even if the concept is satisfied.
 *
 * \see `tcspc::is_processor_v`
 */
template <typename Proc, typename... Events>
concept is_processor = handles_flush<Proc> && handles_events<Proc, Events...>;

/**
 * \brief Helper variable template for `tcspc::is_processor`.
 */
template <typename Proc, typename... Events>
inline constexpr bool is_processor_v = is_processor<Proc, Events...>;

/** @} <!-- group is-processor --> */

/**
 * \defgroup handles-event-list Trait handles_event_list_v
 * \ingroup processor-traits
 * @{
 */

/** \cond implementation-detail */

namespace internal {

template <typename Proc, typename TypeList> struct handles_event_list_impl {
    static_assert(is_type_list<TypeList>);
};

template <typename Proc, typename... Events>
struct handles_event_list_impl<Proc, type_list<Events...>>
    : std::bool_constant<handles_events<Proc, Events...>> {};

} // namespace internal

/** \endcond */

/**
 * \brief Trait variable to check whether a processor handles a list of event
 * types.
 *
 * Determines whether the processor \p Proc handles all of the event types in
 * the `tcspc::type_list` specialization \p EventList.
 *
 * If the event handler(s) exist but any of them has a return type other than
 * `void`, compilation fails.
 *
 * This trait is intentionally retained as a `_v` variable rather than a
 * concept: C++20 does not permit partial specialization of concepts, so a
 * type-list-keyed concept must route through a class-template specialization
 * to unpack the pack and would not subsume with the pack-keyed
 * `tcspc::handles_events` concept. Presenting it as a query trait avoids
 * misleading subsumption expectations.
 *
 * \note A true result indicates that \p Proc handles the events in `EventList`
 * _provided that `Proc` and the relevant `Proc::handle()` overloads can be
 * instantiated_ (if \p Proc is a template class). It is possible that
 * instantiation will fail (due to `static_assert` failures or other issues)
 * even if the result of this trait is true.
 */
template <typename Proc, typename EventList>
inline constexpr bool handles_event_list_v =
    internal::handles_event_list_impl<Proc, EventList>::value;

/** @} <!-- group handles-event-list --> */

/**
 * \defgroup is-processor-of-list Trait is_processor_of_list_v
 * \ingroup processor-traits
 * @{
 */

/** \cond implementation-detail */

namespace internal {

template <typename Proc, typename TypeList> struct is_processor_of_list_impl {
    static_assert(is_type_list<TypeList>);
};

template <typename Proc, typename... Events>
struct is_processor_of_list_impl<Proc, type_list<Events...>>
    : std::bool_constant<is_processor<Proc, Events...>> {};

} // namespace internal

/** \endcond */

/**
 * \brief Trait variable to check whether a processor handles a list of event
 * types and flush.
 *
 * Determines whether the processor \p Proc handles all of the event types in
 * the `tcspc::type_list` specialization \p EventList as well as `flush()`.
 *
 * This is equivalent to the logical AND of `tcspc::handles_event_list_v<Proc,
 * EventList>` and `tcspc::handles_flush<Proc>`.
 *
 * If the event handler(s) and/or `flush()` exist but any of them has a return
 * type other than `void`, compilation fails.
 *
 * This trait is intentionally retained as a `_v` variable rather than a
 * concept: C++20 does not permit partial specialization of concepts, so a
 * type-list-keyed concept must route through a class-template specialization
 * to unpack the pack and would not subsume with the pack-keyed
 * `tcspc::is_processor` concept. Presenting it as a query trait avoids
 * misleading subsumption expectations.
 *
 * \note A true result indicates that \p Proc handles flush and the events in
 * `EventList` _provided that `Proc`, `Proc::flush()`, and the relevant
 * `Proc::handle()` overloads can be instantiated_ (if \p Proc is a template
 * class). It is possible that instantiation will fail (due to `static_assert`
 * failures or other issues) even if the result of this trait is true.
 */
template <typename Proc, typename EventList>
inline constexpr bool is_processor_of_list_v =
    internal::is_processor_of_list_impl<Proc, EventList>::value;

/** @} <!-- group is-processor-of-list --> */

} // namespace tcspc
