/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

namespace flimevt {

/**
 * \brief Processor that applies a macrotime offset to all events.
 *
 * \tparam D downstream processor type
 */
template <typename D> class time_delay {
    macrotime delta;
    D downstream;

  public:
    /**
     * \brief Construct with macrotime offset and downstream processor.
     *
     * \param delta macrotime offset to apply (can be negative)
     * \param downstream downstream processor (moved out)
     */
    explicit time_delay(macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {}

    /** \brief Processor interface */
    template <typename E> void handle_event(E const &event) noexcept {
        E copy(event);
        copy.macrotime += delta;
        downstream.handle_event(copy);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace flimevt
