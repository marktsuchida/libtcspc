/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"

#include <exception>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Downstream> class delay {
    macrotime delta;
    Downstream downstream;

  public:
    explicit delay(macrotime delta, Downstream &&downstream)
        : delta(delta), downstream(std::move(downstream)) {}

    template <typename TimeTaggedEvent>
    void handle_event(TimeTaggedEvent const &event) noexcept {
        TimeTaggedEvent copy(event);
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
 * \ingroup processors-timing
 *
 * All events processed must have a \c macrotime field, and no other fields
 * derived from the macrotime (because only the \c macrotime field will be
 * adjusted).
 *
 * \tparam Downstream downstream processor type
 *
 * \param delta macrotime offset to apply (can be negative)
 *
 * \param downstream downstream processor
 *
 * \return delay processor
 *
 * \inevents
 * \event{All events, passed through with time delay applied}
 * \endevents
 */
template <typename Downstream>
auto delay(macrotime delta, Downstream &&downstream) {
    return internal::delay<Downstream>(delta,
                                       std::forward<Downstream>(downstream));
}

} // namespace tcspc
