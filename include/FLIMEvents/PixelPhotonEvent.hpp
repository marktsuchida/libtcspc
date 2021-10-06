#pragma once

#include "EventSet.hpp"

#include <cstdint>

namespace flimevt {

struct PixelPhotonEvent {
    uint16_t microtime;
    uint16_t route;
    uint32_t x;
    uint32_t y;
    uint32_t frame;
};

struct BeginFrameEvent {};
struct EndFrameEvent {};

using PixelPhotonEvents =
    EventSet<PixelPhotonEvent, BeginFrameEvent, EndFrameEvent>;

} // namespace flimevt
