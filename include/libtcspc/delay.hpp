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

template <typename DataTraits, typename Downstream> class delay {
    typename DataTraits::abstime_type delta;
    Downstream downstream;

  public:
    explicit delay(typename DataTraits::abstime_type delta,
                   Downstream &&downstream)
        : delta(delta), downstream(std::move(downstream)) {}

    template <typename TimeTaggedEvent>
    void handle_event(TimeTaggedEvent const &event) noexcept {
        TimeTaggedEvent copy(event);
        copy.abstime += delta;
        downstream.handle_event(copy);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that applies a abstime offset to all events.
 *
 * \ingroup processors-timing
 *
 * All events processed must have a \c abstime field, and no other fields
 * derived from the abstime (because only the \c abstime field will be
 * adjusted).
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param delta abstime offset to apply (can be negative)
 *
 * \param downstream downstream processor
 *
 * \return delay processor
 *
 * \inevents
 * \event{All events, passed through with time delay applied}
 * \endevents
 */
template <typename DataTraits, typename Downstream>
auto delay(typename DataTraits::abstime_type delta, Downstream &&downstream) {
    return internal::delay<DataTraits, Downstream>(
        delta, std::forward<Downstream>(downstream));
}

} // namespace tcspc
