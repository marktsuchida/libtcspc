#pragma once

#include "EventSet.hpp"

#include <cstdint>

namespace flimevt {

struct PixelPhotonEvent {
    std::uint16_t difftime;
    std::int16_t channel;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t frame;
};

struct BeginFrameEvent {};
struct EndFrameEvent {};

using PixelPhotonEvents =
    EventSet<PixelPhotonEvent, BeginFrameEvent, EndFrameEvent>;

} // namespace flimevt
