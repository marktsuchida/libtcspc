#pragma once

#include <exception>
#include <tuple>
#include <utility>

template <typename... Ds> class BroadcastProcessor {
    std::tuple<Ds...> downstreams;

  public:
    explicit BroadcastPixelPhotonProcessor(Ds &&... downstreams)
        : downstreams{std::move<Ds>(downstreams)...} {}

    template <typename E> void HandleEvent(E const &event) {
        std::apply([&](auto &... s) { (..., s.HandleEvent(event)); },
                   downstreams);
    }

    void HandleEnd(std::exception_ptr error) {
        std::apply([&](auto &... s) { (..., s.HandleEnd(error)); },
                   downstreams);
    }
};
