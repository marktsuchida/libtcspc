/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "type_list.hpp"

#include <concepts>
#include <ostream>

namespace tcspc::internal {

template <typename Event>
concept abstime_stamped = requires { Event::abstime; };

template <typename E>
concept ostreamable = requires(std::ostream &s, E const &e) {
    { s << e } -> std::same_as<std::ostream &>;
};

template <typename EventList>
inline constexpr bool is_ostreamable_list_v = false;

template <typename... Es>
inline constexpr bool is_ostreamable_list_v<type_list<Es...>> =
    (ostreamable<Es> && ...);

} // namespace tcspc::internal
