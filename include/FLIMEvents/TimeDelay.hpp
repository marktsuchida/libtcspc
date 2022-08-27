#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

namespace flimevt {

template <typename D> class TimeDelay {
    Macrotime delta;
    D downstream;

  public:
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
