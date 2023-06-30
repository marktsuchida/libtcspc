/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"

#include <exception>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventSetToGate, typename OpenEvent, typename CloseEvent,
          typename Downstream>
class gate_events {
    bool open;

    Downstream downstream;

  public:
    explicit gate_events(bool initially_open, Downstream &&downstream)
        : open(initially_open), downstream(std::move(downstream)) {}

    void handle_event(OpenEvent const &event) noexcept {
        open = true;
        downstream.handle_event(event);
    }

    void handle_event(CloseEvent const &event) noexcept {
        open = false;
        downstream.handle_event(event);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        if (!contains_event_v<EventSetToGate, OtherEvent> || open)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that gates events depending on current state.
 *
 * \ingroup processors-timing
 *
 * Events belonging to \c EventSetToGate are gated: if an \c OpenEvent was
 * received more recently than an \c CloseEvent, they are passed through;
 * otherwise they are discarded.
 *
 * All events not in \c EventSetToGate are passed through (including \c
 * OpenEvent and \c CloseEvent).
 *
 * \tparam EventSetToGate event types to gate
 *
 * \tparam OpenEvent event type that opens the gate
 *
 * \tparam CloseEvent event type that closes the gate
 *
 * \tparam Downstream downstream processor type
 *
 * \param initially_open whether the gate is open before the first \c OpenEvent
 * or \c CloseEvent event is received
 *
 * \param downstream downstream processor (moved out)
 *
 * \return gate-events processor
 */
template <typename EventSetToGate, typename OpenEvent, typename CloseEvent,
          typename Downstream>
auto gate_events(bool initially_open, Downstream &&downstream) {
    return internal::gate_events<EventSetToGate, OpenEvent, CloseEvent,
                                 Downstream>(
        initially_open, std::forward<Downstream>(downstream));
}

} // namespace tcspc
