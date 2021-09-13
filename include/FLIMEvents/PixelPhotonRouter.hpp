#pragma once

#include "PixelPhotonEvent.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

class PixelPhotonRouter : public PixelPhotonProcessor {
    // Indexed by channel number
    std::vector<std::shared_ptr<PixelPhotonProcessor>> downstreams;

  public:
    // Downstreams indexed by channel number; may be null
    template <typename... T>
    explicit PixelPhotonRouter(T... downstreams)
        : downstreams{{downstreams...}} {}

    explicit PixelPhotonRouter(
        std::vector<std::shared_ptr<PixelPhotonProcessor>> downstreams)
        : downstreams(downstreams) {}

    void HandleBeginFrame() override {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleBeginFrame();
            }
        }
    }

    void HandleEndFrame() override {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleEndFrame();
            }
        }
    }

    void HandlePixelPhoton(PixelPhotonEvent const &event) override {
        auto channel = event.route;
        std::shared_ptr<PixelPhotonProcessor> d;
        try {
            d = downstreams.at(channel);
        } catch (std::out_of_range const &) {
            return;
        }
        if (d) {
            d->HandlePixelPhoton(event);
        }
    }

    void HandleError(std::string const &message) override {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleError(message);
            }
        }
    }

    void HandleFinish() override {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleFinish();
            }
        }
    }
};
