/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

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
class gate_events {
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
    explicit gate_events(bool initiallyOpen, D &&downstream)
        : open(initiallyOpen), downstream(std::move(downstream)) {}

    /**
     * \brief Construct initially closed with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit gate_events(D &&downstream)
        : gate_events(false, std::move(downstream)) {}

    /** \brief Processor interface */
    template <typename E> void handle_event(E const &event) noexcept {
        if (!contains_event_v<ESet, E> || open)
            downstream.handle_event(event);
    }

    /** \brief Processor interface */
    void handle_event(EOpen const &event) noexcept {
        open = true;
        downstream.handle_event(event);
    }

    /** \brief Processor interface */
    void handle_event(EClose const &event) noexcept {
        open = false;
        downstream.handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace flimevt
