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

#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventList, bool Inverted, typename Downstream>
class select {
    static_assert(is_processor_v<Downstream>);

    Downstream downstream;

  public:
    explicit select(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "select");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename AnyEvent,
              typename = std::enable_if_t<
                  is_convertible_to_type_list_member_v<
                      remove_cvref_t<AnyEvent>, EventList> == Inverted ||
                  handles_event_v<Downstream, remove_cvref_t<AnyEvent>>>>
    void handle(AnyEvent &&event) {
        if constexpr (is_convertible_to_type_list_member_v<
                          remove_cvref_t<AnyEvent>, EventList> != Inverted)
            downstream.handle(std::forward<AnyEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that passes a given set of events and discards
 * others.
 *
 * \ingroup processors-filtering
 *
 * \tparam EventList event types to pass
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `EventList`: pass through with no action
 * - Types not in `EventList`: ignore
 * - Flush: pass through with no action
 */
template <typename EventList, typename Downstream>
auto select(Downstream downstream) {
    return internal::select<EventList, false, Downstream>(
        std::move(downstream));
}

/**
 * \brief Create a processor that passes no events.
 *
 * \ingroup processors-filtering
 *
 * The processor does pass end-of-stream, so can be used as a way to detect the
 * end of the stream.
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - All types: ignore
 * - Flush: pass through with no action
 */
template <typename Downstream> auto select_none(Downstream downstream) {
    return internal::select<type_list<>, false, Downstream>(
        std::move(downstream));
}

/**
 * \brief Create a processor that discards a given set of events and passes
 * others.
 *
 * \ingroup processors-filtering
 *
 * \tparam EventList event types to discard
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `EventList`: ignore
 * - Types not in `EventList`: pass through with no action
 * - Flush: pass through with no action
 */
template <typename EventList, typename Downstream>
auto select_not(Downstream downstream) {
    return internal::select<EventList, true, Downstream>(
        std::move(downstream));
}

/**
 * \brief Create a processor that passes all events.
 *
 * \ingroup processors-filtering
 *
 * In other words, this is a no-op processor.
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - All types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Downstream> auto select_all(Downstream downstream) {
    return internal::select<type_list<>, true, Downstream>(
        std::move(downstream));
}

} // namespace tcspc
