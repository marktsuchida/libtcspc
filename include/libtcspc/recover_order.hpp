/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename DataTraits, typename Event, typename Downstream>
class recover_order {
    typename DataTraits::abstime_type window_size;

    // We just use a sorted vector, because the intended use cases do not
    // require buffering large numbers of events.
    std::vector<Event> buf; // Always in ascending abstime order

    Downstream downstream;

  public:
    explicit recover_order(typename DataTraits::abstime_type time_window,
                           Downstream &&downstream)
        : window_size(time_window), downstream(std::move(downstream)) {
        assert(window_size >= 0);
    }

    void handle_event(Event const &event) noexcept {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTraits::abstime_type>);

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
            [&](Event const &e) noexcept { return e.abstime < cutoff; });
        std::for_each(buf.begin(), keep_it, [&](Event const &e) noexcept {
            downstream.handle_event(std::as_const(e));
        });
        buf.erase(buf.begin(), keep_it);

        auto ins_it = std::find_if(buf.rbegin(), buf.rend(),
                                   [&](Event const &e) noexcept {
                                       return e.abstime < event.abstime;
                                   });
        if (ins_it == buf.rend())
            buf.insert(buf.begin(), event);
        else
            buf.insert(ins_it.base(), event);
    }

    // Do not allow other events.

    void handle_end(std::exception_ptr const &error) noexcept {
        std::for_each(buf.begin(), buf.end(), [&](Event const &e) noexcept {
            downstream.handle_event(std::as_const(e));
        });
        buf.clear();
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that sorts events by abstime, provided that they
 * are out of order only within a bounded time window.
 *
 * \ingroup processors-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam Event type of event to sort
 *
 * \tparam Downstream downstream processor type
 *
 * \param time_window maximum abstime by which events are our of order; must
 * not be negative
 *
 * \param downstream downstream processor
 */
template <typename DataTraits, typename Event, typename Downstream>
auto recover_order(typename DataTraits::abstime_type time_window,
                   Downstream &&downstream) {
    return internal::recover_order<DataTraits, Event, Downstream>(
        time_window, std::forward<Downstream>(downstream));
}

} // namespace tcspc
