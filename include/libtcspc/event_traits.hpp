/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace tcspc::internal {

template <typename Event>
concept has_abstime = requires { Event::abstime; };

template <typename Event>
inline constexpr bool has_abstime_v = has_abstime<Event>;

} // namespace tcspc::internal
