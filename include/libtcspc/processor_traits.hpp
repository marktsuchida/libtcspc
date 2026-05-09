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
 * \brief Concept that is satisfied when a processor handles flushing.
 *
 * \ingroup processor-traits
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
 */
template <typename Proc>
concept flushable = requires(Proc p) {
    { p.flush() } -> std::same_as<void>;
};

/**
 * \brief Concept that is satisfied when a processor handles an event type by
 * rvalue reference.
 *
 * \ingroup processor-traits
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
 */
template <typename Proc, typename Event>
concept rvalue_handler_for = requires(Proc p, Event &&e) {
    { p.handle(std::move(e)) } -> std::same_as<void>;
};

/**
 * \brief Concept that is satisfied when a processor handles an event type by
 * const lvalue reference.
 *
 * \ingroup processor-traits
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
 */
template <typename Proc, typename Event>
concept const_handler_for = requires(Proc p, Event const &e) {
    { p.handle(e) } -> std::same_as<void>;
};

/**
 * \brief Concept that is satisfied when a processor handles the given event
 * types.
 *
 * \ingroup processor-traits
 *
 * Determines whether the processor \p Proc handles all of the event types \p
 * Events.
 *
 * This is equivalent to the logical AND of `tcspc::const_handler_for<Proc,
 * Event>` and `tcspc::rvalue_handler_for<Proc, Event>` over each \p Event in
 * \p Events.
 *
 * If the event handler(s) exist but any of them has a return type other than
 * `void`, the concept is not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles the `Events`
 * _provided that `Proc` and the relevant `Proc::handle()` overloads can be
 * instantiated_ (if \p Proc is a template class). It is possible that
 * instantiation will fail (due to `static_assert` failures or other issues)
 * even if the concept is satisfied.
 */
template <typename Proc, typename... Events>
concept handler_for = (... && (const_handler_for<Proc, Events> &&
                               rvalue_handler_for<Proc, Events>));

/**
 * \brief Concept that is satisfied when a processor handles the given event
 * types and flush.
 *
 * \ingroup processor-traits
 *
 * Determines whether the processor \p Proc handles all of the event types \p
 * Events as well as `flush()`.
 *
 * This is equivalent to the logical AND of `tcspc::handler_for<Proc,
 * Events...>` and `tcspc::flushable<Proc>`.
 *
 * If the event handler(s) and/or `flush()` exist but any of them has a return
 * type other than `void`, the concept is not satisfied.
 *
 * \note A satisfied concept indicates that \p Proc handles flush and the
 * `Events` _provided that `Proc`, `Proc::flush()`, and the relevant
 * `Proc::handle()` overloads can be instantiated_ (if \p Proc is a template
 * class). It is possible that instantiation will fail (due to `static_assert`
 * failures or other issues) even if the concept is satisfied.
 */
template <typename Proc, typename... Events>
concept processor = flushable<Proc> && handler_for<Proc, Events...>;

/** \cond implementation-detail */

namespace internal {

template <typename Proc, typename TypeList> struct handles_event_list_impl {
    static_assert(type_list_like<TypeList>);
};

template <typename Proc, typename... Events>
struct handles_event_list_impl<Proc, type_list<Events...>>
    : std::bool_constant<handler_for<Proc, Events...>> {};

} // namespace internal

/** \endcond */

/**
 * \brief Trait variable to check whether a processor handles a list of event
 * types.
 *
 * \ingroup processor-traits
 *
 * Determines whether the processor \p Proc handles all of the event types in
 * the `tcspc::type_list` specialization \p EventList. Equivalent to
 * `tcspc::handler_for<Proc, Events...>` where `Events...` is the parameter
 * pack of \p EventList.
 *
 * If the event handler(s) exist but any of them has a return type other than
 * `void`, compilation fails.
 *
 * \par Why this is a `_v` trait, not a concept
 * Unpacking the type list requires a class-template partial specialization,
 * because C++20 does not permit partial specialization of concepts (only of
 * class and variable templates). A type-list-keyed predicate written as a
 * concept therefore has to route through a helper class template and is
 * structurally `<helper-impl>::value` — a single atomic constraint. It would
 * not subsume with the pack-keyed `tcspc::handler_for` concept, so the two
 * could not participate together in concept-subsumption-arbitrated overload
 * resolution. Presenting this predicate as a `_v` query rather than a
 * peer-named concept avoids implying a subsumption relationship that the
 * language cannot deliver.
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

/** \cond implementation-detail */

namespace internal {

template <typename Proc, typename TypeList> struct is_processor_of_list_impl {
    static_assert(type_list_like<TypeList>);
};

template <typename Proc, typename... Events>
struct is_processor_of_list_impl<Proc, type_list<Events...>>
    : std::bool_constant<processor<Proc, Events...>> {};

} // namespace internal

/** \endcond */

/**
 * \brief Trait variable to check whether a processor handles a list of event
 * types and flush.
 *
 * \ingroup processor-traits
 *
 * Determines whether the processor \p Proc handles all of the event types in
 * the `tcspc::type_list` specialization \p EventList as well as `flush()`.
 * Equivalent to `tcspc::processor<Proc, Events...>` where `Events...` is
 * the parameter pack of \p EventList.
 *
 * If the event handler(s) and/or `flush()` exist but any of them has a return
 * type other than `void`, compilation fails.
 *
 * \par Why this is a `_v` trait, not a concept
 * Unpacking the type list requires a class-template partial specialization,
 * because C++20 does not permit partial specialization of concepts (only of
 * class and variable templates). A type-list-keyed predicate written as a
 * concept therefore has to route through a helper class template and is
 * structurally `<helper-impl>::value` — a single atomic constraint. It would
 * not subsume with the pack-keyed `tcspc::processor` concept, so the two
 * could not participate together in concept-subsumption-arbitrated overload
 * resolution. Presenting this predicate as a `_v` query rather than a
 * peer-named concept avoids implying a subsumption relationship that the
 * language cannot deliver.
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

} // namespace tcspc
