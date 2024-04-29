/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"
#include "type_list.hpp"
#include "variant_event.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace tcspc {

namespace internal {

template <typename EventList, typename Downstream> class multiplex {
    static_assert(handles_event_v<Downstream, variant_event<EventList>>);
    static_assert(handles_flush_v<Downstream>);

    Downstream downstream;

  public:
    explicit multiplex(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "multiplex");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename Event, typename = std::enable_if_t<type_list_contains_v<
                                  EventList, internal::remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        downstream.handle(
            variant_event<EventList>(std::forward<Event>(event)));
    }

    void flush() { downstream.flush(); }
};

template <typename Downstream> class demultiplex {
    static_assert(handles_flush_v<Downstream>);

    Downstream downstream;

  public:
    explicit demultiplex(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "demultiplex");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    // In theory we should be able to support rvalues and move the event out of
    // the variant. In practice I cannot figure out how to do this in C++17
    // (without C++20's explicit lambda template parameters).

    template <typename EL> void handle(variant_event<EL> const &event) {
        static_assert(
            handles_event_list_v<Downstream, EL>,
            "demultiplex only accepts variant_event whose event list is a subset of the events handled by the downstream");
        std::visit([&](auto const &e) { downstream.handle(e); }, event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that passes events as a single variant type.
 *
 * \ingroup processors-mux
 *
 * This can be used, for example, to buffer more than one type of event in a
 * stream. The emitted events are of the single type
 * `tcspc::variant_event<EventList>`.
 *
 * \see `tcspc::demultiplex()`
 *
 * \tparam EventList event types to combine
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `EventList`: pass through wrapped in
 *   `tcspc::variant_event<EventList>`
 * - Flush: pass through with no action
 */
template <typename EventList, typename Downstream>
auto multiplex(Downstream &&downstream) {
    static_assert(type_list_size_v<EventList> > 0,
                  "multiplex requires non-empty event list");
    return internal::multiplex<EventList, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that transforms an event variant type back to
 * individual event types.
 *
 * \ingroup processors-mux
 *
 * This reverses the effect of `tcspc::multiplex()`, accepting
 * `tcspc::variant_event` and emitting the stored events. Only
 * `tcspc::variant_event` specializations whose type list is a subset of the
 * events handled by \p downstream are handled.
 *
 * \see `tcspc::multiplex()`
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::variant_event<TL>`, where all types in the `tcspc::type_list` `TL`
 *   are handled by `downstream`: pass through unwrapped
 * - Flush: pass through with no action
 */
template <typename Downstream> auto demultiplex(Downstream &&downstream) {
    return internal::demultiplex<Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
