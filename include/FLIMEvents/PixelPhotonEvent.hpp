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

struct BeginFrameEvent {};
struct EndFrameEvent {};

// Receiver of pixel-assigned photon events
class PixelPhotonProcessor {
  public:
    virtual ~PixelPhotonProcessor() = default;

    virtual void HandleEvent(BeginFrameEvent const &event) = 0;
    virtual void HandleEvent(EndFrameEvent const &event) = 0;
    virtual void HandleEvent(PixelPhotonEvent const &event) = 0;
    virtual void HandleError(std::string const &message) = 0;
    virtual void HandleFinish() = 0;
};

template <std::size_t N>
class BroadcastPixelPhotonProcessor final : public PixelPhotonProcessor {
    std::array<std::shared_ptr<PixelPhotonProcessor>, N> downstreams;

  public:
    // All downstreams must be non-null
    template <typename... T>
    explicit BroadcastPixelPhotonProcessor(T... downstreams)
        : downstreams{{downstreams...}} {}

    void HandleEvent(BeginFrameEvent const &) final {
        for (auto &d : downstreams) {
            d->HandleEvent();
        }
    }

    void HandleEvent(EndFrameEvent const &) final {
        for (auto &d : downstreams) {
            d->HandleEvent();
        }
    }

    void HandleEvent(PixelPhotonEvent const &event) final {
        for (auto &d : downstreams) {
            d->HandleEvent(event);
        }
    }

    void HandleError(std::string const &message) final {
        for (auto &d : downstreams) {
            d->HandleError(message);
        }
    }

    void HandleFinish() final {
        for (auto &d : downstreams) {
            d->HandleFinish();
        }
    }
};
