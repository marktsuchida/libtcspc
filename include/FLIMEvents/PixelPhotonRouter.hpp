#pragma once

#include "BroadcastProcessor.hpp"
#include "PixelPhotonEvent.hpp"

#include <exception>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace flimevt {

template <typename... Ds> class PixelPhotonRouter {
    std::tuple<Ds...> downstreams;

  private:
    template <size_t N = 0>
    void HandlePixelPhotonForChannel(size_t channel,
                                     PixelPhotonEvent const &event) noexcept {
        if (channel == N) {
            downstreams.get<N>().HandleEvent(event);
            return;
        }

        if constexpr (N + 1 < std::tuple_size_v<decltype(sinks)>) {
            HandlePixelPhotonForChannel<N + 1>(channel, event);
        }
    }

  public:
    explicit PixelPhotonRouter(Ds &&... downstreams)
        : downstreams{std::move<Ds>(downstreams)...} {}

    template <typename E> void HandleEvent(E const &event) noexcept {
        std::apply([&](auto &... s) { (..., s.HandleEvent(event)); },
                   downstreams);
    }

    void HandleEvent(PixelPhotonEvent const &event) noexcept {
        HandlePixelPhotonForChannel(event.route, event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        std::apply([&](auto &... s) { (..., s.HandleEnd(error)); },
                   downstreams);
    }
};

} // namespace flimevt
