/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "introspect.hpp"
#include "type_list.hpp"

#include <utility>

namespace tcspc {

namespace internal {

template <typename GatedEventList, typename OpenEvent, typename CloseEvent,
          typename Downstream>
class gate {
    bool open;

    Downstream downstream;

  public:
    explicit gate(bool initially_open, Downstream downstream)
        : open(initially_open), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "gate");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(OpenEvent const &event) {
        open = true;
        downstream.handle(event);
    }

    void handle(CloseEvent const &event) {
        open = false;
        downstream.handle(event);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        if constexpr (type_list_contains_v<GatedEventList, OtherEvent>) {
            if (open)
                downstream.handle(event);
        } else {
            downstream.handle(event);
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that gates events depending on current state.
 *
 * \ingroup processors-timing
 *
 * The processor keeps an internal boolean state: the "gate". The initial state
 * of the gate is determined by \e initially_open. When an \c OpenEvent is
 * received, the gate is opened. When a \c CloseEvent is received, the gate is
 * closed.
 *
 * Events belonging to \c GatedEventList are gated: they are passed through if
 * and only if the gate is currently open.
 *
 * All events not in \c GatedEventList are passed through (including \c
 * OpenEvent and \c CloseEvent).
 *
 * \tparam GatedEventList event types to gate
 *
 * \tparam OpenEvent event type that opens the gate
 *
 * \tparam CloseEvent event type that closes the gate
 *
 * \tparam Downstream downstream processor type
 *
 * \param initially_open whether the gate is open before the first \c OpenEvent
 * or \c CloseEvent event is received
 *
 * \param downstream downstream processor
 *
 * \return gate-events processor
 */
template <typename GatedEventList, typename OpenEvent, typename CloseEvent,
          typename Downstream>
auto gate(bool initially_open, Downstream &&downstream) {
    return internal::gate<GatedEventList, OpenEvent, CloseEvent, Downstream>(
        initially_open, std::forward<Downstream>(downstream));
}

} // namespace tcspc
