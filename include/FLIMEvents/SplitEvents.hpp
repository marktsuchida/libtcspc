#pragma once

#include "EventSet.hpp"

#include <exception>
#include <utility>

namespace flimevt {

template <typename ESet, typename D0, typename D1> class SplitEvents {
    D0 downstream0;
    D1 downstream1;

  public:
    explicit SplitEvents(D0 &&downstream0, D1 &&downstream1)
        : downstream0(std::move(downstream0)),
          downstream1(std::move(downstream1)) {}

    template <typename E> void HandleEvent(E const &event) noexcept {
        if constexpr (ContainsEventV<ESet, E>)
            downstream1.HandleEvent(event);
        else
            downstream0.HandleEvent(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream0.HandleEnd(error);
        downstream1.HandleEnd(error);
    }
};

} // namespace flimevt
