#pragma once

#include "Common.hpp"
#include "TCSPCEvents.hpp"

#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace flimevt {

/**
 * \brief Processor that converts MarkerEvent with specific channel to a
 * specified event type.
 *
 * This can be used, for example, to convert specific marker events into events
 * representing frame, line, or pixel markers for FLIM. Each instance converts
 * a single marker channel to a single event type.
 *
 * The output event type \c EOut must have a \c macrotime field of type
 * \c Macrotime, and must be brace-initializable with a macrotime value (as in
 * \c EOut{123} ).
 *
 * \tparam EOut output event type for matching marker events
 * \tparam D downstream processor type
 */
template <typename EOut, typename D> class TranslateMarker {
    std::int32_t const chan;
    D downstream;

    static_assert(std::is_same_v<decltype(EOut{0}.macrotime), Macrotime>,
                  "EOut must have a macrotime field of type Macrotime");
    static_assert(EOut{42}.macrotime == 42,
                  "EOut must be initializeable with macrotime");

  public:
    /**
     * \brief Construct with marker channel and downstream processor.
     *
     * \param channel channel of marker events to convert to EOut events
     * \param downstream downstream processor (moved out)
     */
    explicit TranslateMarker(std::int32_t channel, D &&downstream)
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
