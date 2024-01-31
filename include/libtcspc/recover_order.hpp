/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "introspect.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename EventSet, typename DataTraits, typename Downstream>
class recover_order {
    using abstime_type = typename DataTraits::abstime_type;
    abstime_type window_size;

    // We just use a sorted vector, because the intended use cases do not
    // require buffering large numbers of events.
    using element_type = std::conditional_t<event_set_size_v<EventSet> == 1,
                                            event_set_element_t<0, EventSet>,
                                            event_variant<EventSet>>;
    std::vector<element_type> buf; // Always in ascending abstime order
    // For error checking
    abstime_type last_emitted_time = std::numeric_limits<abstime_type>::min();

    Downstream downstream;

    // Abstract away single event vs variant element_type.
    template <typename F> static auto call_or_visit(F func) {
        if constexpr (event_set_size_v<EventSet> == 1)
            return [func](auto const &e) { return func(e); };
        else
            return [func](auto const &e) { return std::visit(func, e); };
    }

  public:
    explicit recover_order(abstime_type time_window, Downstream downstream)
        : window_size(time_window), downstream(std::move(downstream)) {
        if (window_size < 0)
            throw std::invalid_argument(
                "recover_order time_window must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "recover_order");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename Event> void handle(Event const &event) {
        static_assert(contains_event_v<EventSet, Event>);
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);
        if (event.abstime < last_emitted_time) {
            throw std::runtime_error(
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
        auto keep_it = std::find_if_not(
            buf.begin(), buf.end(),
            call_or_visit([&](auto const &e) { return e.abstime < cutoff; }));

        std::for_each(buf.begin(), keep_it, call_or_visit([&](auto const &e) {
                          downstream.handle(e);
                          last_emitted_time = e.abstime;
                      }));
        buf.erase(buf.begin(), keep_it);

        auto ins_it = std::find_if(buf.rbegin(), buf.rend(),
                                   call_or_visit([&](auto const &e) {
                                       return e.abstime < event.abstime;
                                   }));
        if (ins_it == buf.rend())
            buf.insert(buf.begin(), event);
        else
            buf.insert(ins_it.base(), event);
    }

    // Do not allow other events.

    void flush() {
        std::for_each(
            buf.begin(), buf.end(),
            call_or_visit([&](auto const &e) { downstream.handle(e); }));
        buf.clear();
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that sorts events by abstime, provided that they
 * are out of order only within a bounded time window.
 *
 * \ingroup processors-timing
 *
 * \tparam EventSet events to sort
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param time_window maximum abstime by which events are our of order; must
 * not be negative
 *
 * \param downstream downstream processor
 */
template <typename EventSet, typename DataTraits = default_data_traits,
          typename Downstream>
auto recover_order(typename DataTraits::abstime_type time_window,
                   Downstream &&downstream) {
    static_assert(event_set_size_v<EventSet> > 0,
                  "recover_order requires non-empty event set");
    return internal::recover_order<EventSet, DataTraits, Downstream>(
        time_window, std::forward<Downstream>(downstream));
}

} // namespace tcspc
