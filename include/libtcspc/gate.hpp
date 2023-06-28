/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"

#include <exception>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Es, typename EOpen, typename EClose, typename D>
class gate_events {
    bool open;

    D downstream;

  public:
    explicit gate_events(bool initially_open, D &&downstream)
        : open(initially_open), downstream(std::move(downstream)) {}

    template <typename E> void handle_event(E const &event) noexcept {
        if (!contains_event_v<Es, E> || open)
            downstream.handle_event(event);
    }

    void handle_event(EOpen const &event) noexcept {
        open = true;
        downstream.handle_event(event);
    }

    void handle_event(EClose const &event) noexcept {
        open = false;
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
 * Events belonging to \c Es are gated: if an \c EOpen was received more
 * recently than an \c EClose, they are passed through; otherwise they are
 * discarded.
 *
 * All events not in \c Es are passed through (including \c EOpen and \c
 * EClose).
 *
 * \tparam Es event types to gate
 * \tparam EOpen event type that opens the gate
 * \tparam EClose event type that closes the gate
 * \tparam D downstream processor type
 * \param initially_open whether the gate is open before the first \c EOpen
 * or \c EClose event is received
 * \param downstream downstream processor (moved out)
 * \return gate-events processor
 */
template <typename Es, typename EOpen, typename EClose, typename D>
auto gate_events(bool initially_open, D &&downstream) {
    return internal::gate_events<Es, EOpen, EClose, D>(
        initially_open, std::forward<D>(downstream));
}

} // namespace tcspc
