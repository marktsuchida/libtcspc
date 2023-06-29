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

template <typename EventSetToSplit, typename Downstream0, typename Downstream1>
class split_events {
    Downstream0 downstream0;
    Downstream1 downstream1;

  public:
    explicit split_events(Downstream0 &&downstream0, Downstream1 &&downstream1)
        : downstream0(std::move(downstream0)),
          downstream1(std::move(downstream1)) {}

    template <typename AnyEvent>
    void handle_event(AnyEvent const &event) noexcept {
        if constexpr (contains_event_v<EventSetToSplit, AnyEvent>)
            downstream1.handle_event(event);
        else
            downstream0.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream0.handle_end(error);
        downstream1.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that splits events into two streams according to
 * event type.
 *
 * \tparam EventSetToSplit event set specifying event types that should be
 * routed to downstream processor 1
 * \tparam Downstream0 type of downstream processor 0
 * \tparam Downstream1 type of downstream processor 1
 * \param downstream0 the downstream receiving events not in EventSetToSplit
 * (moved out)
 * \param downstream1 the downstream receiving events in EventSetToSplit (moved
 * out)
 * \return split-events processor
 */
template <typename EventSetToSplit, typename Downstream0, typename Downstream1>
auto split_events(Downstream0 &&downstream0, Downstream1 &&downstream1) {
    return internal::split_events<EventSetToSplit, Downstream0, Downstream1>(
        std::forward<Downstream0>(downstream0),
        std::forward<Downstream1>(downstream1));
}

} // namespace tcspc
