/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "errors.hpp"
#include "int_arith.hpp"
#include "introspect.hpp"
#include "numeric_traits.hpp"
#include "processor.hpp"
#include "type_list.hpp"
#include "variant_event.hpp"

#include <algorithm>
#include <concepts>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename EventList, typename NumericTraits, typename Downstream>
    requires is_processor_of_list_v<Downstream, EventList>
class recover_order {
    static_assert(type_list_like<EventList>);
    static_assert(is_move_constructible_list_v<EventList>,
                  "recover_order requires every event in EventList to be "
                  "move-constructible (events are buffered in a vector)");
    static_assert(is_move_assignable_list_v<EventList>,
                  "recover_order requires every event in EventList to be "
                  "move-assignable (the buffer is kept sorted by shifting)");
    // Copy-constructibility is required only for const-lvalue inputs (which
    // are copied into the buffer); checked per-call in handle() so that
    // move-only events fed as rvalues are accepted.

    using abstime_type = typename NumericTraits::abstime_type;
    abstime_type window_size;

    // We just use a sorted vector, because the intended use cases do not
    // require buffering large numbers of events.
    // Always in ascending abstime order:
    std::vector<variant_or_single_event<EventList>> buf;

    // For error checking
    abstime_type last_emitted_time = std::numeric_limits<abstime_type>::min();

    Downstream downstream;

  public:
    explicit recover_order(arg::time_window<abstime_type> time_window,
                           Downstream downstream)
        : window_size(time_window.value), downstream(std::move(downstream)) {
        if (window_size < 0)
            throw std::invalid_argument(
                "recover_order time_window must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "recover_order");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename Event>
        requires convertible_to_type_list_member<std::remove_cvref_t<Event>,
                                                 EventList>
    void handle(Event &&event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);
        if constexpr (std::is_lvalue_reference_v<Event>) {
            static_assert(
                std::copy_constructible<std::remove_cvref_t<Event>>,
                "recover_order copies const-lvalue inputs into its buffer; "
                "pass the event as an rvalue or make it copy-constructible");
        }
        if (event.abstime < last_emitted_time) {
            throw data_validation_error(
                "recover_order encountered event outside of time window");
        }

        // We perform a sliding-window version of insertion sort, enabled by
        // the known time bound of out-of-order events.

        // Both finding the events that are ready to emit and finding the
        // position to insert the new one could be done with log complexity
        // using std::lower_bound() and std::upper_bound(), but we expect the
        // buffer to be small in the anticipated use cases, so prefer to do
        // simple linear searches. (This choice could be made compile-time
        // selectable.)

        auto const cutoff = pairing_cutoff(event.abstime, window_size);
        auto keep_it =
            std::find_if_not(buf.begin(), buf.end(), [&](auto const &v) {
                return visit_variant_or_single_event(
                    [&](auto const &e) { return e.abstime < cutoff; }, v);
            });

        std::for_each(buf.begin(), keep_it, [&](auto &v) {
            visit_variant_or_single_event(
                [&]<typename E>(E &&e) {
                    last_emitted_time = e.abstime;
                    downstream.handle(std::forward<E>(e));
                },
                std::move(v));
        });
        buf.erase(buf.begin(), keep_it);

        auto ins_it =
            std::find_if(buf.rbegin(), buf.rend(), [&](auto const &v) {
                return visit_variant_or_single_event(
                    [&](auto const &e) { return e.abstime < event.abstime; },
                    v);
            });
        if (ins_it == buf.rend())
            buf.insert(buf.begin(), std::forward<Event>(event));
        else
            buf.insert(ins_it.base(), std::forward<Event>(event));
    }

    // Do not allow other events.

    void flush() {
        std::for_each(buf.begin(), buf.end(), [&](auto &v) {
            visit_variant_or_single_event(
                [&]<typename E>(E &&e) {
                    downstream.handle(std::forward<E>(e));
                },
                std::move(v));
        });
        buf.clear();
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that sorts events by abstime, provided that they
 * are out of order only within a bounded time window.
 *
 * \ingroup processors-time-corr
 *
 * \tparam EventList events to sort; every event type must be
 * move-constructible and move-assignable (events are buffered and reordered in
 * a vector). Const-lvalue inputs are additionally copied into the buffer, so a
 * non-copy-constructible event type must be passed as an rvalue.
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`
 *
 * \tparam Downstream downstream processor type
 *
 * \param time_window maximum abstime by which events are out of order; must
 * not be negative
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event` (with `abstime` field): buffer and forward in `abstime` order once
 *   \p time_window has elapsed; throw `data_validation_error` if order could
 *   not be maintained due to event outside of time window
 * - Flush: emit any buffered `Event`s in `abstime` order; pass through
 */
template <typename EventList, typename NumericTraits = default_numeric_traits,
          typename Downstream>
auto recover_order(
    arg::time_window<typename NumericTraits::abstime_type> time_window,
    Downstream downstream) {
    static_assert(type_list_size_v<EventList> > 0,
                  "recover_order requires non-empty event list");
    return internal::recover_order<EventList, NumericTraits, Downstream>(
        time_window, std::move(downstream));
}

} // namespace tcspc
