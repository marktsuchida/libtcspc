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

template <typename EventSetToSplit, typename Downstream0, typename Downstream1>
class split {
    Downstream0 downstream0;
    Downstream1 downstream1;

  public:
    explicit split(Downstream0 &&downstream0, Downstream1 &&downstream1)
        : downstream0(std::move(downstream0)),
          downstream1(std::move(downstream1)) {}

    template <typename AnyEvent> void handle(AnyEvent const &event) {
        if constexpr (contains_event_v<EventSetToSplit, AnyEvent>) {
            try {
                downstream1.handle(event);
            } catch (end_processing const &) {
                downstream0.flush();
                throw;
            }

        } else {
            try {
                downstream0.handle(event);
            } catch (end_processing const &) {
                downstream1.flush();
                throw;
            }
        }
    }

    void flush() {
        std::exception_ptr end;
        try {
            downstream0.flush();
        } catch (end_processing const &) {
            end = std::current_exception();
        }
        try {
            downstream1.flush();
        } catch (end_processing const &) {
            if (end) // Both threw; keep first.
                std::rethrow_exception(end);
            throw;
        }
        if (end)
            std::rethrow_exception(end);
    }
};

} // namespace internal

/**
 * \brief Create a processor that splits events into two streams according to
 * event type.
 *
 * \ingroup processors-basic
 *
 * \tparam EventSetToSplit event set specifying event types that should be
 * routed to downstream processor 1
 *
 * \tparam Downstream0 type of downstream processor 0
 *
 * \tparam Downstream1 type of downstream processor 1
 *
 * \param downstream0 the downstream receiving events not in EventSetToSplit
 * (moved out)
 *
 * \param downstream1 the downstream receiving events in EventSetToSplit (moved
 * out)
 *
 * \return split-events processor
 */
template <typename EventSetToSplit, typename Downstream0, typename Downstream1>
auto split(Downstream0 &&downstream0, Downstream1 &&downstream1) {
    return internal::split<EventSetToSplit, Downstream0, Downstream1>(
        std::forward<Downstream0>(downstream0),
        std::forward<Downstream1>(downstream1));
}

} // namespace tcspc
