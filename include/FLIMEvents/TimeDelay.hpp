#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

namespace flimevt {

/**
 * \brief Processor that applies a macrotime offset to all events.
 *
 * \tparam D downstream processor type
 */
template <typename D> class TimeDelay {
    Macrotime delta;
    D downstream;

  public:
    /**
     * \brief Construct with macrotime offset and downstream processor.
     *
     * \param delta macrotime offset to apply (can be negative)
     * \param downstream downstream processor (moved out)
     */
    explicit TimeDelay(Macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {}

    template <typename E> void HandleEvent(E const &event) noexcept {
        E copy(event);
        copy.macrotime += delta;
        downstream.HandleEvent(copy);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

} // namespace flimevt
