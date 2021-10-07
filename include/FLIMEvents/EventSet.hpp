#pragma once

#include <type_traits>
#include <variant>

namespace flimevt {

// Type to express collection of events
template <typename... Events> class EventSet {
    EventSet() = delete;

  public:
    template <template <typename...> typename Tmpl, typename... Args>
    using TemplateOfEvents = Tmpl<Args..., Events...>;
};

template <typename ESet>
using EventVariant = typename ESet::template TemplateOfEvents<std::variant>;

namespace internal {

template <typename Event, typename... Events>
struct ContainsEvent : std::disjunction<std::is_same<Event, Events>...> {};

} // namespace internal

template <typename ESet, typename Event>
struct ContainsEvent
    : ESet::template TemplateOfEvents<internal::ContainsEvent, Event> {};

template <typename ESet, typename Event>
static constexpr bool ContainsEventV = ContainsEvent<ESet, Event>::value;

} // namespace flimevt
