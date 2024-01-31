/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"
#include "introspect.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace tcspc {

namespace internal {

template <typename EventSet, typename Downstream> class multiplex {
    static_assert(handles_event_v<Downstream, event_variant<EventSet>>);

    Downstream downstream;

  public:
    explicit multiplex(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "multiplex");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void handle(Event const &event) {
        downstream.handle(event_variant<EventSet>(event));
    }

    void flush() { downstream.flush(); }
};

template <typename EventSet, typename Downstream> class demultiplex {
    static_assert(handles_event_set_v<Downstream, EventSet>);

    Downstream downstream;

  public:
    explicit demultiplex(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "demultiplex");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(event_variant<EventSet> const &event) {
        std::visit([&](auto const &e) { downstream.handle(e); }, event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that passes events as a single variant type.
 *
 * \ingroup processors-basic
 *
 * This can be used, for example, to buffer more than one type of event in a
 * stream. The emitted events are of the single type \c
 * event_variant<EventSet>.
 *
 * \see demultiplex
 *
 * \tparam EventSet event types to combine
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return multiplex processor
 */
template <typename EventSet, typename Downstream>
auto multiplex(Downstream &&downstream) {
    static_assert(event_set_size_v<EventSet> > 0,
                  "multiplex requires non-empty event set");
    return internal::multiplex<EventSet, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that transforms an event variant type back to
 * individual event types.
 *
 * \ingroup processors-basic
 *
 * This reverses the effect of \c multiplex: it accepts \c
 * event_variant<EventSet> and emits the individual events in \c EventSet.
 *
 * \see multiplex
 *
 * \tparam EventSet event types to separate
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return demultiplex processor
 */
template <typename EventSet, typename Downstream>
auto demultiplex(Downstream &&downstream) {
    return internal::demultiplex<EventSet, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
