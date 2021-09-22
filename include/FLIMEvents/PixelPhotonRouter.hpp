#pragma once

#include "PixelPhotonEvent.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

class PixelPhotonRouter final : public PixelPhotonProcessor {
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

    void HandleEvent(BeginFrameEvent const &event) final {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleEvent(event);
            }
        }
    }

    void HandleEvent(EndFrameEvent const &event) final {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleEvent(event);
            }
        }
    }

    void HandleEvent(PixelPhotonEvent const &event) final {
        auto channel = event.route;
        std::shared_ptr<PixelPhotonProcessor> d;
        try {
            d = downstreams.at(channel);
        } catch (std::out_of_range const &) {
            return;
        }
        if (d) {
            d->HandleEvent(event);
        }
    }

    void HandleError(std::exception_ptr exception) final {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleError(exception);
            }
        }
    }

    void HandleFinish() final {
        for (auto &d : downstreams) {
            if (d) {
                d->HandleFinish();
            }
        }
    }
};
