#pragma once

#include "EventSet.hpp"

#include <cstdint>

namespace flimevt {

struct PixelPhotonEvent {
    std::uint16_t microtime;
    std::uint16_t route;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t frame;
};

struct BeginFrameEvent {};
struct EndFrameEvent {};

using PixelPhotonEvents =
    EventSet<PixelPhotonEvent, BeginFrameEvent, EndFrameEvent>;

} // namespace flimevt
