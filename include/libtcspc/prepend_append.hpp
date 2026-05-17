/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "processor.hpp"

#include <type_traits>

namespace tcspc {

namespace internal {

template <typename Event, typename Downstream>
    requires processor<Downstream, Event>
class prepend {
    bool prepended = false;
    Downstream downstream;

    // Cold data after downstream:
    Event evt;

  public:
    explicit prepend(Event event, Downstream downstream)
        : downstream(std::move(downstream)), evt(std::move(event)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "prepend");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename AnyEvent>
        requires handler_for<Downstream, std::remove_cvref_t<AnyEvent>>
    void handle(AnyEvent &&event) {
        if (not prepended) {
            downstream.handle(std::move(evt));
            prepended = true;
        }
        downstream.handle(std::forward<AnyEvent>(event));
    }

    void flush() { downstream.flush(); }
};

template <typename Event, typename Downstream>
    requires processor<Downstream, Event>
class append {
    Downstream downstream;

    // Cold data after downstream:
    Event evt;

  public:
    explicit append(Event event, Downstream downstream)
        : downstream(std::move(downstream)), evt(std::move(event)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "append");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename AnyEvent>
        requires handler_for<Downstream, std::remove_cvref_t<AnyEvent>>
    void handle(AnyEvent &&event) {
        downstream.handle(std::forward<AnyEvent>(event));
    }

    void flush() {
        downstream.handle(std::move(evt));
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that inserts an event at the beginning of the
 * stream.
 *
 * \ingroup processors-core
 *
 * All events are passed through. Before the first event is passed through, the
 * given \p event is emitted.
 *
 * \tparam Event type of event to prepend (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param event event to prepend
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Any type: if the first event ever, emit \p event; pass through
 * - Flush: pass through with no action
 */
template <typename Event, typename Downstream>
auto prepend(Event event, Downstream downstream) {
    return internal::prepend<Event, Downstream>(std::move(event),
                                                std::move(downstream));
}

/**
 * \brief Create a processor that inserts an event at the end of the stream.
 *
 * \ingroup processors-core
 *
 * All events are passed through. Before flushing, the given \p event is
 * emitted.
 *
 * \note \p event is only appended upon a flush; if processing is ended by
 * `tcspc::end_of_processing` being thrown by a _downstream_ processor, this
 * processor has no effect.
 *
 * \tparam Event type of event to append (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param event event to append
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Any type: pass through with no action
 * - Flush: emit \p event; pass through
 */
template <typename Event, typename Downstream>
auto append(Event event, Downstream downstream) {
    return internal::append<Event, Downstream>(std::move(event),
                                               std::move(downstream));
}

} // namespace tcspc
