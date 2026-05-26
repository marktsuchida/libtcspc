/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "context.hpp"
#include "introspect.hpp"
#include "processor.hpp"

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace tcspc {

/**
 * \brief Access for `tcspc::record_last()` processor data.
 *
 * \ingroup context-access
 */
template <typename Event> class record_last_access {
    std::function<std::optional<Event>()> get_fn;

  public:
    /** \private */
    template <typename F>
    explicit record_last_access(F get_func) : get_fn(get_func) {}

    /** \brief Return the last event observed, or empty if none was. */
    auto get() -> std::optional<Event> { return get_fn(); }
};

namespace internal {

template <typename Event, typename Downstream> class record_last {
    std::optional<Event> last;

    Downstream downstream;

    // Cold data after downstream.
    access_tracker<record_last_access<Event>> trk;

  public:
    explicit record_last(access_tracker<record_last_access<Event>> &&tracker,
                         Downstream downstream)
        : downstream(std::move(downstream)), trk(std::move(tracker)) {
        trk.register_access_factory([](auto &tracker) {
            auto *self =
                LIBTCSPC_OBJECT_FROM_TRACKER(record_last, trk, tracker);
            return record_last_access<Event>([self] { return self->last; });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "record_last");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E>
        requires handler_for<Downstream, std::remove_cvref_t<E>>
    void handle(E &&event) {
        if constexpr (std::is_same_v<std::remove_cvref_t<E>, Event>)
            last = event; // Retain a copy.
        downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that remembers the last event of a given type.
 *
 * \ingroup processors-stats
 *
 * The processor passes through all events. It retains a copy of the last event
 * of type \p Event that passed through.
 *
 * The retained event can be retrieved through a `tcspc::record_last_access`
 * obtained from the `tcspc::context` from which \p tracker was obtained. It is
 * empty (`std::nullopt`) until an event of type \p Event has been observed.
 *
 * Because the processor stores a copy of every event of the requested type
 * that passes through (and retrieval via the access also returns a copy), it
 * is best suited to events that are cheap to copy and that occur infrequently
 * — most naturally a single result event such as a
 * `tcspc::concluding_histogram_event`.
 *
 * \tparam Event the event type to remember (must be specified explicitly)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param tracker access tracker for later access of the recorded event
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: retain a copy; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Event, typename Downstream>
auto record_last(access_tracker<record_last_access<Event>> &&tracker,
                 Downstream downstream) {
    return internal::record_last<Event, Downstream>(std::move(tracker),
                                                    std::move(downstream));
}

} // namespace tcspc
