/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "time_tagged_events.hpp"

#include <cstddef>
#include <limits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename DataTypes, typename Downstream>
class regulate_time_reached {
    using abstime_type = typename DataTypes::abstime_type;

    abstime_type interval_thresh;
    std::size_t count_thresh;

    abstime_type exact_reached = std::numeric_limits<abstime_type>::min();
    abstime_type next_time_thresh = std::numeric_limits<abstime_type>::min();
    std::size_t emitted_since_prev_time_reached = 0;
    std::size_t seen_since_prev_time_reached = 0;

    Downstream downstream;

    // Called for all upstream times seen.
    void handle_time_reached(abstime_type abstime) {
        ++seen_since_prev_time_reached;
        if (abstime >= next_time_thresh ||
            emitted_since_prev_time_reached >= count_thresh) {
            downstream.handle(time_reached_event<DataTypes>{abstime});
            next_time_thresh = add_sat(abstime, interval_thresh);
            emitted_since_prev_time_reached = 0;
            seen_since_prev_time_reached = 0;
        }
        exact_reached = abstime;
    }

  public:
    explicit regulate_time_reached(abstime_type interval_threshold,
                                   std::size_t count_threshold,
                                   Downstream downstream)
        : interval_thresh(interval_threshold), count_thresh(count_threshold),
          downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "regulate_time_reached");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(time_reached_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::abstime_type, abstime_type>);
        handle_time_reached(event.abstime);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);
        downstream.handle(event);
        ++emitted_since_prev_time_reached;
        handle_time_reached(event.abstime);
    }

    void flush() {
        // Emit time-reached for last seen event in order to convey the (best
        // known) stream end time on all downstream paths.
        // Only do so if we received at least one event and the last emitted
        // was something other than time-reached.
        if (exact_reached > std::numeric_limits<abstime_type>::min() &&
            seen_since_prev_time_reached > 0)
            downstream.handle(time_reached_event<DataTypes>{exact_reached});
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that regulates the frequency of time-reached
 * events.
 *
 * \ingroup processors-timeline
 *
 * This processor can be used to ensure that the event stream contains
 * `tcspc::time_reached_event` at reasonable abstime intervals (to keep live
 * displays responsive) and at reasonable frequency in terms of event count (to
 * prevent unbounded growth of buffer size at merge processors). It also
 * removes excess time-reached events based on the same criteria.
 *
 * This type of regulation is necessary when there is no guarantee that the
 * upstream input contains time-reached events at regular intervals and there
 * are processors downstream that sort events from multiple streams by abstime:
 * specifically, `tcspc::merge()` or `tcspc::merge_n()`. This is because merge
 * processors are unable to emit the buffered events until they know that all
 * earlier upstream events have been seen.
 *
 * Processors that have multiple downstreams (such as `tcspc::route()`) should
 * broadcast time-reached events so that each branch carries information about
 * elapsed time, allowing merging processors to limit buffering to reasonable
 * latency and capacity.
 *
 * Note that the abstime-based criterion is not perfect and depends on the
 * upstream containing (any) events at reasonable abstime intervals, because
 * the time-reached event is only emitted upon receiving an upstream event past
 * the interval threshold.
 *
 * When procssing stored data, or when live display update is not a
 * requirement, the \p interval_threshold can be set to the maximum value of
 * the `abstime_type` to disable the criterion, relying solely on the \p
 * count_threshold. It is recommended that \p count_threshold be set to a
 * reasonable limit even when \p interval_threshold is used as the main
 * criterion.
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 *
 * \tparam Downstream downstream processor type
 *
 * \param interval_threshold a time-reached event is emitted at the next
 * opportunity if at least this abstime interval has elapsed since the
 * previously emitted time-reached event
 *
 * \param count_threshold a time-reached event is emitted when this many events
 * have been emitted since the previously emitted time-reached event
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::time_reached_event<DT>`: emit as
 *   `tcspc::time_reached_event<DataTypes>` with rate limiting
 * - All types with `abstime` field: passed through, possibly followed by
 *   `tcspc::time_reached_event<DataTypes>`
 * - Flush: emit `tcspc::time_reached_event<DataTypes>` with time of last
 *   passed event; pass through
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto regulate_time_reached(typename DataTypes::abstime_type interval_threshold,
                           std::size_t count_threshold,
                           Downstream downstream) {
    return internal::regulate_time_reached<DataTypes, Downstream>(
        interval_threshold, count_threshold,
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
