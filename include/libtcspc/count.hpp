/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "context.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
class count_up_to {
    // Do not require handling of ResetEvent, as it may not be used at all.
    static_assert(is_processor_v<Downstream, TickEvent, FireEvent>);

    std::uint64_t count;
    std::uint64_t init;
    std::uint64_t thresh;
    std::uint64_t lmt;

    Downstream downstream;

    template <typename Abstime> void pre_tick(Abstime abstime) {
        if constexpr (!FireAfterTick) {
            if (count == thresh)
                downstream.handle(FireEvent{abstime});
        }
    }

    template <typename Abstime> void post_tick(Abstime abstime) {
        ++count;

        if constexpr (FireAfterTick) {
            if (count == thresh)
                downstream.handle(FireEvent{abstime});
        }

        if (count == lmt)
            count = init;
    }

  public:
    explicit count_up_to(arg::threshold<std::uint64_t> threshold,
                         arg::limit<std::uint64_t> limit,
                         arg::initial_count<std::uint64_t> initial_count,
                         Downstream downstream)
        : count(initial_count.value), init(initial_count.value),
          thresh(threshold.value), lmt(limit.value),
          downstream(std::move(downstream)) {
        if (init >= lmt)
            throw std::invalid_argument(
                "count_up_to limit must be greater than initial_count");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "count_up_to");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void handle(TickEvent const &event) {
        pre_tick(event.abstime);
        downstream.handle(event);
        post_tick(event.abstime);
    }

    void handle(TickEvent &&event) {
        auto const abstime = event.abstime;
        pre_tick(abstime);
        downstream.handle(std::move(event));
        post_tick(abstime);
    }

    template <typename E, typename = std::enable_if_t<
                              handles_event_v<Downstream, remove_cvref_t<E>>>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, ResetEvent>) {
            count = init;
        }
        downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that counts a specific event and emits an event
 * when the count reaches a threshold.
 *
 * \ingroup processors-timing
 *
 * All events (including \p TickEvent and \p ResetEvent) are passed through.
 *
 * \p TickEvent and \p FireEvent must have an `abstime` field. \p FireEvent
 * must be brace-initializable with abstime (as in `FireEvent{123}`).
 *
 * The internal counter starts at \p initial_count and is incremented when a \p
 * TickEvent is passed through. Just before or after the \p TickEvent is
 * emitted (depending on whether \p FireAfterTick is false or true), the count
 * is compared to the \p threshold and if equal, \p FireEvent is emitted. The
 * abstime of the \p FireEvent is set equal to the \p TickEvent that triggered
 * it.
 *
 * After incrementing the count and processing the threshold, if the count
 * equals \p limit, then the count is reset to \p initial_count. Automatic
 * resetting can be effectively disabled by setting \p limit to
 * `std::numeric_limits<std::uint64_t>::max()`.
 *
 * The \p limit must be greater than \p initial_count. When \p FireAfterTick is
 * false, \p threshold should be greater than or equal to \p initial_count and
 * less than \p limit; otherwise \p FireEvent is never emitted. When \p
 * FireAfterTick is true, \p threshold should be greater than \p initial_count
 * and less than or equal to \p limit; otherwise \p FireEvent is never emitted.
 *
 * When a \p ResetEvent is received (and passed through), the count is reset to
 * \p initial_count. No \p FireEvent is emitted on reset, but if \p
 * FireAfterTick is false and \p threshold is set equal to \p initial_count,
 * then a \p FireEvent is emitted on the next \p TickEvent received.
 *
 * \remark Applications of this processor include converting fast raster clocks
 * (e.g., pixel clock) to slower ones (e.g., line clock) or detecting when a
 * desired number of detection events have been accumulated.
 *
 * \see `tcspc::count_down_to()`
 *
 * \tparam TickEvent the event type to count
 *
 * \tparam FireEvent the event type to emit when the count reaches \p threshold
 *
 * \tparam ResetEvent an event type that causes the count to be reset to \p
 * initial_count
 *
 * \tparam FireAfterTick whether to emit \p FireEvent after passing through \p
 * TickEvent, rather than before
 *
 * \tparam Downstream downstream processor type
 *
 * \param threshold the count value at which to emit \p FireEvent
 *
 * \param limit the count value at which to reset to \p initial_count; must be
 * greater than \p initial_count
 *
 * \param initial_count the value at which the count starts and to which it is
 * reset
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `TickEvent`: increment counter; pass through; before or after doing so,
 *   emit `FireEvent` if at threshold; if at limit reset counter
 * - `ResetEvent`: reset counter; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
auto count_up_to(arg::threshold<std::uint64_t> threshold,
                 arg::limit<std::uint64_t> limit,
                 arg::initial_count<std::uint64_t> initial_count,
                 Downstream &&downstream) {
    return internal::count_up_to<TickEvent, FireEvent, ResetEvent,
                                 FireAfterTick, Downstream>(
        threshold, limit, initial_count, std::forward<Downstream>(downstream));
}

/**
 * \brief Like `tcspc::count_up_to()`, but decrement the count on each tick
 * event.
 *
 * \ingroup processors-timing
 *
 * All behavior is symmetric to `tcspc::count_up_to()`. \p limit must be less
 * than \p initial_count.
 *
 * \see `tcspc::count_up_to()`
 */
template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
auto count_down_to(arg::threshold<std::uint64_t> threshold,
                   arg::limit<std::uint64_t> limit,
                   arg::initial_count<std::uint64_t> initial_count,
                   Downstream &&downstream) {
    // Alter parameters to emulate count down using count up.
    if (limit.value >= initial_count.value)
        throw std::invalid_argument(
            "count_down_to limit must be less than initial_count");
    if (threshold.value > initial_count.value ||
        threshold.value < limit.value) {
        // Counter will never fire; no change to threshold needed.
    } else {
        // Mirror threshold around midpoint of initial_count and limit.
        threshold.value =
            limit.value + (initial_count.value - threshold.value);
    }
    using std::swap;
    swap(initial_count.value, limit.value);

    return internal::count_up_to<TickEvent, FireEvent, ResetEvent,
                                 FireAfterTick, Downstream>(
        threshold, limit, initial_count, std::forward<Downstream>(downstream));
}

/**
 * \brief Access for `tcspc::count()` processor data.
 *
 * \ingroup context-access
 */
class count_access {
    std::function<std::uint64_t()> count_fn;

  public:
    /** \private */
    template <typename Func>
    explicit count_access(Func count_func) : count_fn(count_func) {}

    /**
     * \brief Return the count value of the associated processor.
     */
    auto count() -> std::uint64_t { return count_fn(); }
};

namespace internal {

template <typename Event, typename Downstream> class count {
    static_assert(is_processor_v<Downstream, Event>);

    std::uint64_t ct = 0;

    Downstream downstream;

    // Cold data after downstream.
    access_tracker<count_access> trk;

  public:
    explicit count(access_tracker<count_access> &&tracker,
                   Downstream downstream)
        : downstream(std::move(downstream)), trk(std::move(tracker)) {
        trk.register_access_factory([](auto &tracker) {
            auto *self = LIBTCSPC_OBJECT_FROM_TRACKER(count, trk, tracker);
            return count_access([self] { return self->ct; });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "count");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E, typename = std::enable_if_t<
                              handles_event_v<Downstream, remove_cvref_t<E>>>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, Event>)
            ++ct;
        downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that counts events of a given type.
 *
 * \ingroup processors-stats
 *
 * The count can be retrieved through a `tcspc::count_access` retrieved from
 * the `tcspc::context` from which \p tracker was obtained.
 *
 * Note that the count is incremented \e before the events of type \p Event are
 * sent to the downstream processor. This means that the event will be counted
 * even if it subsequently results in an error or end-of-stream in a downstream
 * processor.
 *
 * \tparam Event type of event to count
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param tracker access tracker for later access of the count result
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: increment the count; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Event, typename Downstream>
auto count(access_tracker<count_access> &&tracker, Downstream &&downstream) {
    return internal::count<Event, Downstream>(
        std::move(tracker), std::forward<Downstream>(downstream));
}

} // namespace tcspc
