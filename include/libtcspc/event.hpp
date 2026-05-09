/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace tcspc::internal {

template <typename Event>
concept abstime_stamped = requires { Event::abstime; };

} // namespace tcspc::internal
