/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "EventSet.hpp"

#include <exception>
#include <utility>

namespace flimevt {

/**
 * \brief Processor that splits events into two streams according to event
 * type.
 *
 * \tparam ESet event set specifying event types that should be routed to
 * downstream processor 1
 * \tparam D0 type of downstream processor 0
 * \tparam D1 type of downstream processor 1
 */
template <typename ESet, typename D0, typename D1> class split_events {
    D0 downstream0;
    D1 downstream1;

  public:
    /**
     * \brief Construct with downstream processors.
     *
     * \param downstream0 the downstream receiving events not in ESet (moved
     * out)
     * \param downstream1 the downstream receiving events in ESet (moved out)
     */
    explicit split_events(D0 &&downstream0, D1 &&downstream1)
        : downstream0(std::move(downstream0)),
          downstream1(std::move(downstream1)) {}

    /** \brief Processor interface */
    template <typename E> void handle_event(E const &event) noexcept {
        if constexpr (contains_event_v<ESet, E>)
            downstream1.handle_event(event);
        else
            downstream0.handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        downstream0.handle_end(error);
        downstream1.handle_end(error);
    }
};

} // namespace flimevt
