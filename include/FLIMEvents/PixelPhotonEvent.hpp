#pragma once

#include "DecodedEvent.hpp"

#include <string>


struct PixelPhotonEvent {
    uint16_t microtime;
    uint16_t route;
    uint32_t x;
    uint32_t y;
    uint32_t frame;
};


// Receiver of pixel-assigned photon events
class PixelPhotonProcessor {
public:
    virtual ~PixelPhotonProcessor() = default;

    virtual void HandleBeginFrame() = 0;
    virtual void HandleEndFrame() = 0;
    virtual void HandlePixelPhoton(PixelPhotonEvent const& event) = 0;
    virtual void HandleError(std::string const& message) = 0;
    virtual void HandleFinish() = 0;
};
