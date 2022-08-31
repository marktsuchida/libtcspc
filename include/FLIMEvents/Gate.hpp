#pragma once

#include "EventSet.hpp"

#include <exception>
#include <type_traits>
#include <utility>

namespace flimevt {

/**
 * \brief Processor that gates events depending on current state.
 *
 * Events belonging to \c ESet are gated: if an \c EOpen was received more
 * recently than an \c EClose, they are passed through; otherwise they are
 * discarded.
 *
 * All events not in \c ESet are passed through (including \c EOpen and \c
 * EClose).
 *
 * \tparam ESet event types to gate
 * \tparam EOpen event type that opens the gate
 * \tparam EClose event type that closes the gate
 * \tparam D downstream processor type
 */
template <typename ESet, typename EOpen, typename EClose, typename D>
class GateEvents {
    bool open;

    D downstream;

  public:
    /**
     * \brief Construct with initial state and downstream processor.
     *
     * \param initiallyOpen whether the gate is open before the first \c EOpen
     * or \c EClose event is received
     * \param downstream downstream processor (moved out)
     */
    explicit GateEvents(bool initiallyOpen, D &&downstream)
        : open(initiallyOpen), downstream(std::move(downstream)) {}

    /**
     * \brief Construct initially closed with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit GateEvents(D &&downstream)
        : GateEvents(false, std::move(downstream)) {}

    template <typename E> void HandleEvent(E const &event) noexcept {
        if (!ContainsEventV<ESet, E> || open)
            downstream.HandleEvent(event);
    }

    void HandleEvent(EOpen const &event) noexcept {
        open = true;
        downstream.HandleEvent(event);
    }
    void HandleEvent(EClose const &event) noexcept {
        open = false;
        downstream.HandleEvent(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

} // namespace flimevt
