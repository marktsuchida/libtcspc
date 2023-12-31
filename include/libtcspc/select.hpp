/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"

#include <exception>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventSet, bool Inverted, typename Downstream> class select {
    Downstream downstream;

  public:
    explicit select(Downstream downstream)
        : downstream(std::move(downstream)) {}

    template <typename AnyEvent> void handle(AnyEvent const &event) {
        if constexpr (contains_event_v<EventSet, AnyEvent> != Inverted)
            downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that passes a given set of events and discards
 * others.
 *
 * \ingroup processors-basic
 *
 * \see select_not
 * \see select_none
 * \see select_all
 *
 * \tparam EventSet event types to pass
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename EventSet, typename Downstream>
auto select(Downstream &&downstream) {
    return internal::select<EventSet, false, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that passes no events.
 *
 * \ingroup processors-basic
 *
 * The processor does pass end-of-stream, so can be used as a way to detect the
 * end of the stream.
 *
 * \see select_all
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename Downstream> auto select_none(Downstream &&downstream) {
    return internal::select<event_set<>, false, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that discards a given set of events and passes
 * others.
 *
 * \ingroup processors-basic
 *
 * \see select
 * \see select_none
 * \see select_all
 *
 * \tparam EventSet event types to discard
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename EventSet, typename Downstream>
auto select_not(Downstream &&downstream) {
    return internal::select<EventSet, true, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that passes all events.
 *
 * \ingroup processors-basic
 *
 * In other words, this is a no-op processor.
 *
 * \see select_none
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename Downstream> auto select_all(Downstream &&downstream) {
    return internal::select<event_set<>, true, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
