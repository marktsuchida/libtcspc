/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"
#include "type_list.hpp"

#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename GatedEventList, typename OpenEvent, typename CloseEvent,
          typename Downstream>
class gate {
    static_assert(is_type_list_v<GatedEventList>);
    // We do not require Downstream to handle all of GatedEventList.
    static_assert(handles_events_v<Downstream, OpenEvent, CloseEvent>);

    bool open;

    Downstream downstream;

  public:
    explicit gate(arg::initially_open<bool> initially_open,
                  Downstream downstream)
        : open(initially_open.value), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "gate");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E, typename = std::enable_if_t<
                              handles_event_v<Downstream, remove_cvref_t<E>>>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, OpenEvent>) {
            open = true;
            downstream.handle(std::forward<E>(event));
        } else if constexpr (std::is_convertible_v<remove_cvref_t<E>,
                                                   CloseEvent>) {
            open = false;
            downstream.handle(std::forward<E>(event));
        } else if constexpr (is_convertible_to_type_list_member_v<
                                 remove_cvref_t<E>, GatedEventList>) {
            if (open)
                downstream.handle(std::forward<E>(event));
        } else {
            downstream.handle(std::forward<E>(event));
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that gates events depending on current state.
 *
 * \ingroup processors-filtering
 *
 * The processor keeps an internal boolean state: the _gate_. The initial state
 * of the gate is determined by \p initially_open. When an \p OpenEvent is
 * received, the gate is opened. When a \p CloseEvent is received, the gate is
 * closed.
 *
 * Events belonging to \p GatedEventList are gated: they are passed through if
 * and only if the gate is currently open.
 *
 * All events not in \p GatedEventList are passed through (including \p
 * OpenEvent and \p CloseEvent).
 *
 * \tparam GatedEventList event types to gate
 *
 * \tparam OpenEvent event type that opens the gate
 *
 * \tparam CloseEvent event type that closes the gate
 *
 * \tparam Downstream downstream processor type
 *
 * \param initially_open whether the gate is open before the first \p OpenEvent
 * or \p CloseEvent event is received
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `OpenEvent`: open the gate; pass through
 * - `CloseEvent`: close the gate; pass through
 * - Types in `GatedEventList`: pass through if gate open; otherwise no action
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename GatedEventList, typename OpenEvent, typename CloseEvent,
          typename Downstream>
auto gate(arg::initially_open<bool> initially_open, Downstream downstream) {
    return internal::gate<GatedEventList, OpenEvent, CloseEvent, Downstream>(
        initially_open, std::move(downstream));
}

} // namespace tcspc
