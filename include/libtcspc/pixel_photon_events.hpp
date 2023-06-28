/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"

#include <cstdint>

//! \cond TO_BE_REMOVED
// NOLINTBEGIN

namespace tcspc {

struct pixel_photon_event {
    std::uint16_t difftime;
    std::int16_t channel;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t frame;
};

struct begin_frame_event {};
struct end_frame_event {};

using pixel_photon_events =
    event_set<pixel_photon_event, begin_frame_event, end_frame_event>;

} // namespace tcspc

// NOLINTEND
//! \endcond
