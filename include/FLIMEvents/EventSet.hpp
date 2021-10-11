#pragma once

#include "ApplyClassTemplateToTupleElements.hpp"

#include <exception>
#include <tuple>
#include <type_traits>
#include <variant>

namespace flimevt {

template <typename... Events> using EventSet = std::tuple<Events...>;

template <typename ESet>
using EventVariant = ApplyClassTemplateToTupleElementsT<std::variant, ESet>;

namespace internal {

template <typename Event, typename... Events>
struct EventIsOneOf : std::disjunction<std::is_same<Event, Events>...> {};

} // namespace internal

template <typename ESet, typename Event>
struct ContainsEvent
    : ApplyClassTemplateToTupleElementsT<internal::EventIsOneOf, ESet, Event> {
};

template <typename ESet, typename Event>
static constexpr bool ContainsEventV = ContainsEvent<ESet, Event>::value;

template <typename Proc, typename Event, typename = void>
struct HandlesEvent : std::false_type {};

template <typename Proc, typename Event>
struct HandlesEvent<Proc, Event,
                    std::void_t<decltype(std::declval<Proc>().HandleEvent(
                        std::declval<Event>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().HandleEvent(
                             std::declval<Event>()))> {};

template <typename Proc, typename Event>
static constexpr bool HandlesEventV = HandlesEvent<Proc, Event>::value;

template <typename Proc, typename = void>
struct HandlesEnd : std::false_type {};

template <typename Proc>
struct HandlesEnd<Proc, std::void_t<decltype(std::declval<Proc>().HandleEnd(
                            std::declval<std::exception_ptr>()))>>
    : std::is_same<void, decltype(std::declval<Proc>().HandleEnd(
                             std::declval<std::exception_ptr>()))> {};

template <typename Proc>
static constexpr bool HandlesEndV = HandlesEnd<Proc>::value;

namespace internal {

template <typename Proc, typename... Events>
struct HandlesEvents : std::conjunction<HandlesEvent<Proc, Events>...> {};

template <typename Proc, typename... Events>
struct HandlesEventsAndEnd
    : std::conjunction<HandlesEvents<Proc, Events...>, HandlesEnd<Proc>> {};

} // namespace internal

template <typename Proc, typename ESet>
struct HandlesEventSet
    : ApplyClassTemplateToTupleElementsT<internal::HandlesEventsAndEnd, ESet,
                                         Proc> {};

template <typename Proc, typename ESet>
static constexpr bool HandlesEventSetV = HandlesEventSet<Proc, ESet>::value;

} // namespace flimevt
