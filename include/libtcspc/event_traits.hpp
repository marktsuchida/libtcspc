/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <type_traits>

namespace tcspc::internal {

template <typename Event, typename = void>
struct has_abstime : std::false_type {};

template <typename Event>
struct has_abstime<Event, std::void_t<decltype(Event::abstime)>>
    : std::true_type {};

template <typename Event>
static constexpr bool has_abstime_v = has_abstime<Event>::value;

} // namespace tcspc::internal
