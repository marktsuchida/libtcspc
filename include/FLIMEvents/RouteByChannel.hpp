#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

namespace flimevt {

/**
 * \brief Processor that routes events to downstream processors according to
 * channel.
 *
 * This processor holds multiple downstream processors and a mapping from
 * channel numbers to downstream indices. Events of type \c ERoute are passed
 * only to the downstream indexed by the mapped channel number.
 *
 * If the channel does not map to a downstream index, or there is no processor
 * at the mapped index, then the \c ERoute event is discarded.
 *
 * Events other than \c ERoute are broadcast to all downstream processors.
 *
 * \tparam ERouted event type to route by channel
 * \tparam Ds downstream processor types
 */
template <typename ERouted, typename... Ds> class RouteByChannel {
    std::vector<std::int16_t> const channels;
    std::tuple<Ds...> downstreams;

    template <std::size_t I = 0>
    void HandlePhoton(std::size_t index, ERouted const &event) noexcept {
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
     * The channel mapping is specified as an std::vector of channel numbers.
     * The channel at index \e i in the vector is mapped to downstream index \e
     * i. (This has the limitation that only one channel can be mapped to each
     * downstream.)
     *
     * Thus, if channels contains <tt>{5, -3}</tt> and an \c ERouted event is
     * received with channel equal to \c -3, then it is routed to downstream
     * processor 1 (counting from 0). If fewer than 2 downstream processors
     * were given, such an \c ERouted event would be discarded.
     *
     * \param channels channel mapping
     * \param downstreams downstream processors (moved out)
     */
    explicit RouteByChannel(std::vector<std::int16_t> const &channels,
                            Ds &&...downstreams)
        : channels(channels), downstreams{std::move(downstreams)...} {}

    void HandleEvent(ERouted const &event) noexcept {
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
