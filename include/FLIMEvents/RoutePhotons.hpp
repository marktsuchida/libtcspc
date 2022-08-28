#pragma once

#include "TCSPCEvents.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

namespace flimevt {

// Route valid photons to downstreams according to channel.
// Channel index is position of channel in the channels vector.
// If channel is not in channels vector, photon is ignored.
// If channel index exceeds max index of downstreams, photon is ignored.

template <typename... Ds> class RoutePhotons {
    std::vector<std::int16_t> const channels;
    std::tuple<Ds...> downstreams;

    template <std::size_t I = 0>
    void HandlePhoton(std::size_t index,
                      ValidPhotonEvent const &event) noexcept {
        if (index == I) {
            std::get<I>(downstreams).HandleEvent(event);
            return;
        }
        if constexpr (I + 1 < std::tuple_size_v<decltype(downstreams)>)
            HandlePhoton<I + 1>(index, event);
    }

  public:
    explicit RoutePhotons(std::vector<std::int16_t> const &channels,
                          Ds &&...downstreams)
        : channels(channels), downstreams{std::move(downstreams)...} {}

    void HandleEvent(ValidPhotonEvent const &event) noexcept {
        auto chan = event.channel;
        auto it = std::find(channels.cbegin(), channels.cend(), chan);
        if (it != channels.cend())
            HandlePhoton(std::distance(channels.cbegin(), it), event);
    }

    template <typename E> void HandleEvent(E const &event) noexcept {
        std::apply([&](auto &...s) { (..., s.HandleEvent(event)); },
                   downstreams);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        std::apply([&](auto &...s) { (..., s.HandleEnd(error)); },
                   downstreams);
    }
};

} // namespace flimevt
