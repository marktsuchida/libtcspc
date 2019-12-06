#pragma once

#include "DecodedEvent.hpp"

#include <array>
#include <cstdlib>
#include <memory>
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


template <std::size_t N>
class BroadcastPixelPhotonProcessor : public PixelPhotonProcessor {
    std::array<std::shared_ptr<PixelPhotonProcessor>, N> downstreams;

public:
    template <typename... T>
    BroadcastPixelPhotonProcessor(T... downstreams) :
        downstreams{ {downstreams...} }
    {}

    void HandleBeginFrame() override {
        for (auto& d : downstreams) {
            d->HandleBeginFrame();
        }
    }

    void HandleEndFrame() override {
        for (auto& d : downstreams) {
            d->HandleEndFrame();
        }
    }

    void HandlePixelPhoton(PixelPhotonEvent const& event) override {
        for (auto& d : downstreams) {
            d->HandlePixelPhoton(event);
        }
    }

    void HandleError(std::string const& message) override {
        for (auto& d : downstreams) {
            d->HandleError(message);
        }
    }

    void HandleFinish() override {
        for (auto& d : downstreams) {
            d->HandleFinish();
        }
    }
};
