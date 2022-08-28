#pragma once

#include "Common.hpp"
#include "TCSPCEvents.hpp"

#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace flimevt {

template <typename EOut, typename D> class TranslateMarker {
    std::uint32_t const chan;
    D downstream;

    static_assert(std::is_same_v<decltype(EOut{0}.macrotime), Macrotime>,
                  "EOut must have a macrotime field of type Macrotime");
    static_assert(EOut{42}.macrotime == 42,
                  "EOut must be initializeable with macrotime");

  public:
    explicit TranslateMarker(std::uint32_t channel, D &&downstream)
        : chan(channel), downstream(std::move(downstream)) {}

    void HandleEvent(MarkerEvent const &event) noexcept {
        if (event.channel == chan) {
            EOut e{event.macrotime};
            downstream.HandleEvent(e);
        } else {
            downstream.HandleEvent(event);
        }
    }

    template <typename E> void HandleEvent(E const &event) noexcept {
        downstream.HandleEvent(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

} // namespace flimevt
