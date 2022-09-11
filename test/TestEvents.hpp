/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "FLIMEvents/Common.hpp"
#include "FLIMEvents/EventSet.hpp"

#include <ostream>
#include <type_traits>
#include <variant>

namespace flimevt::test {

template <unsigned N> struct Event { macrotime macrotime; };
using Events01 = event_set<Event<0>, Event<1>>;
using Events23 = event_set<Event<2>, Event<3>>;
using Events0123 = event_set<Event<0>, Event<1>, Event<2>, Event<3>>;

template <typename E,
          typename = std::enable_if_t<contains_event_v<Events0123, E>>>
inline std::ostream &operator<<(std::ostream &os, E const &e) {
    return os << typeid(e).name() << '{' << e.macrotime << '}';
}

inline std::ostream &operator<<(std::ostream &os,
                                event_variant<Events01> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

inline std::ostream &operator<<(std::ostream &os,
                                event_variant<Events23> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

inline std::ostream &operator<<(std::ostream &os,
                                event_variant<Events0123> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

template <typename E,
          typename = std::enable_if_t<contains_event_v<Events0123, E>>>
inline bool operator==(E const &lhs, E const &rhs) {
    return lhs.macrotime == rhs.macrotime;
}

} // namespace flimevt::test
