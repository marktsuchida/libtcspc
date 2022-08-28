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

/**
 * \brief Processor that routes valid photon events to downstream processors
 * according to channel.
 *
 * This processor holds a mapping from channel numbers to contiguous indices
 * starting at zero. If a ValidPhotonEvent is received with channel \e c and \e
 * c maps to index \e i, then the event is sent to the downstream processor at
 * position \e i.
 *
 * If the channel does not map to an index, or there is no processor at the
 * index, then the ValidPhotonEvent is discarded.
 *
 * Events other than ValidPhotonEvent are broadcast to all downstream
 * processors.
 *
 * \tparam Ds downstream processor types
 */
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
    /**
     * \brief Construct with channel mapping and downstream processors.
     *
     * The channel mapping is specified as a std::vector of channel numbers.
     * The channel at index \e i in the vector is mapped to downstream index \e
     * i.
     *
     * Thus, if channels contains <tt>{ 5, -3 }</tt> and a ValidPhotonEvent is
     * received with channel \c -3, then it is routed to downstream processor 1
     * (counting from 0). If fewer than 2 downstream processors were given,
     * such a ValidPhotonEvent would be discarded.
     *
     * \param channels channel mapping
     * \param downstreams downstream processors (moved out)
     */
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
