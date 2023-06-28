/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"

#include <exception>
#include <utility>

namespace flimevt {

namespace internal {

template <typename Es, typename D0, typename D1> class split_events {
    D0 downstream0;
    D1 downstream1;

  public:
    explicit split_events(D0 &&downstream0, D1 &&downstream1)
        : downstream0(std::move(downstream0)),
          downstream1(std::move(downstream1)) {}

    template <typename E> void handle_event(E const &event) noexcept {
        if constexpr (contains_event_v<Es, E>)
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
 * \tparam Es event set specifying event types that should be routed to
 * downstream processor 1
 * \tparam D0 type of downstream processor 0
 * \tparam D1 type of downstream processor 1
 * \param downstream0 the downstream receiving events not in Es (moved
 * out)
 * \param downstream1 the downstream receiving events in Es (moved out)
 * \return split-events processor
 */
template <typename Es, typename D0, typename D1>
auto split_events(D0 &&downstream0, D1 &&downstream1) {
    return internal::split_events<Es, D0, D1>(std::forward<D0>(downstream0),
                                              std::forward<D1>(downstream1));
}

} // namespace flimevt
