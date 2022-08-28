#pragma once

#include "EventSet.hpp"

#include <exception>
#include <utility>

namespace flimevt {

/**
 * \brief Processor that splits events into two streams according to event
 * type.
 *
 * \tparam ESet event set specifying event types that should be routed to
 * downstream processor 1
 * \tparam D0 type of downstream processor 0
 * \tparam D1 type of downstream processor 1
 */
template <typename ESet, typename D0, typename D1> class SplitEvents {
    D0 downstream0;
    D1 downstream1;

  public:
    /**
     * \brief Construct with downstream processors.
     *
     * \param downstream0 the downstream receiving events not in ESet (moved
     * out)
     * \param downstream1 the downstream receiving events in ESet (moved out)
     */
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
