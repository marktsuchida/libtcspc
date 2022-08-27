#pragma once

#include "FLIMEvents/Common.hpp"
#include "FLIMEvents/EventSet.hpp"

#include <ostream>
#include <type_traits>
#include <variant>

template <unsigned N> struct Event { flimevt::Macrotime macrotime; };
using Events0 = flimevt::EventSet<Event<0>>;
using Events1 = flimevt::EventSet<Event<1>>;
using Events01 = flimevt::EventSet<Event<0>, Event<1>>;
using Events23 = flimevt::EventSet<Event<2>, Event<3>>;
using Events0123 = flimevt::EventSet<Event<0>, Event<1>, Event<2>, Event<3>>;

template <typename E,
          typename = std::enable_if_t<flimevt::ContainsEventV<Events0123, E>>>
inline std::ostream &operator<<(std::ostream &os, E const &e) {
    return os << typeid(e).name() << '{' << e.macrotime << '}';
}

inline std::ostream &operator<<(std::ostream &os,
                                flimevt::EventVariant<Events01> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

inline std::ostream &operator<<(std::ostream &os,
                                flimevt::EventVariant<Events23> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

inline std::ostream &
operator<<(std::ostream &os, flimevt::EventVariant<Events0123> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

template <typename E,
          typename = std::enable_if_t<flimevt::ContainsEventV<Events0123, E>>>
inline bool operator==(E const &lhs, E const &rhs) {
    return lhs.macrotime == rhs.macrotime;
}
