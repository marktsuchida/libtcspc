/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"

#include <exception>
#include <utility>

namespace flimevt {

namespace internal {

template <typename D> class time_delay {
    macrotime delta;
    D downstream;

  public:
    explicit time_delay(macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {}

    template <typename E> void handle_event(E const &event) noexcept {
        E copy(event);
        copy.macrotime += delta;
        downstream.handle_event(copy);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that applies a macrotime offset to all events.
 *
 * \tparam D downstream processor type
 * \param delta macrotime offset to apply (can be negative)
 * \param downstream downstream processor (moved out)
 * \return time-delay processor
 */
template <typename D> auto time_delay(macrotime delta, D &&downstream) {
    return internal::time_delay<D>(delta, std::forward<D>(downstream));
}

} // namespace flimevt
