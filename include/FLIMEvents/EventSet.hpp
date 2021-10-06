#pragma once

namespace flimevt {

// Type to express collection of events
template <typename... Events> class EventSet {
    EventSet() = delete;

  public:
    template <template <typename...> typename Tmpl, typename... Args>
    using TemplateOfEvents = Tmpl<Args..., Events...>;
};

} // namespace flimevt
