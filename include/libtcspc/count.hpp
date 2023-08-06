/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
class count_up_to {
    std::uint64_t count;
    std::uint64_t init;
    std::uint64_t thresh;
    std::uint64_t limit;

    Downstream downstream;

  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit count_up_to(std::uint64_t threshold, std::uint64_t limit,
                         std::uint64_t initial_count, Downstream &&downstream)
        : count(initial_count), init(initial_count), thresh(threshold),
          limit(limit), downstream(std::move(downstream)) {
        if (init >= limit)
            throw std::invalid_argument(
                "count_up_to limit must be greater than initial_count");
    }

    void handle(TickEvent const &event) {
        if constexpr (!FireAfterTick) {
            if (count == thresh)
                downstream.handle(FireEvent{event.abstime});
        }

        downstream.handle(event);
        ++count;

        if constexpr (FireAfterTick) {
            if (count == thresh)
                downstream.handle(FireEvent{event.abstime});
        }

        if (count == limit)
            count = init;
    }

    void handle(ResetEvent const &event) {
        count = init;
        downstream.handle(event);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
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
 * All events (including \c TickEvent and \c ResetEvent) are passed through.
 *
 * \c TickEvent and \c FireEvent must have a abstime field. \c FireEvent must
 * be brace-initializable with abstime (as in \c FireEvent{123} ).
 *
 * The count is incremented when a \c TickEvent is passed through. Just before
 * or after the \c TickEvent is emitted (depending on whether \c FireAfterTick
 * is false or true), the count is compared to the \e threshold and if equal,
 * \c FireEvent is emitted. The abstime of the \c FireEvent is set equal to
 * the \c TickEvent that triggered it.
 *
 * After incrementing the count and processing the threshold, if the count
 * equals \e limit, then the count is reset to \e initial_count. Automatic
 * resetting can be effectively disabled by setting \e limit to \c
 * std::uint64_t{-1} .
 *
 * The \e limit must be greater than \e initial_count. When \c FireAfterTick is
 * false, \e threshold should be greater than or equal to \e initial_count and
 * less than \e limit; otherwise \c FireEvent is never emitted. When \c
 * FireAfterTick is true, \e threshold should be greater than \e initial_count
 * and less than or equal to \e limit; otherwise \c FireEvent is never emitted.
 *
 * When a \c ResetEvent is received (and passed through), the count is reset to
 * \e initial_count. No \c FireEvent is emitted on reset, but if \c
 * FireAfterTick is false and \e threshold is set equal to \e initial_count,
 * then a \c FireEvent is emitted on the next \c TickEvent received.
 *
 * \see count_down_to
 *
 * \tparam TickEvent the event type to count
 *
 * \tparam FireEvent the event type to emit when the count reaches \e threshold
 *
 * \tparam ResetEvent an event type that causes the count to be reset to \e
 * initial_count
 *
 * \tparam FireAfterTick whether to emit \c FireEvent after passing through \c
 * TickEvent, rather than before
 *
 * \tparam Downstream downstream processor type
 *
 * \param threshold the count value at which to emit \c FireEvent
 *
 * \param limit the count value at which to reset to \e initial_count; must be
 * greater than \e initial_count
 *
 * \param initial_count the value at which the count starts and to which it is
 * reset
 *
 * \param downstream downstream processor
 *
 * \return count-event processor
 *
 * \inevents
 * \event{TickEvent, causes the count to be incremented; passed through}
 * \event{ResetEvent, causes the count to be reset to \e initial_count; passed
 * through}
 * \event{All other events, passed through}
 * \endevents
 *
 * \outevents
 * \event{FireEvent, emitted when the count reaches \e threshold}
 * \event{TickEvent, passed through}
 * \event{ResetEvent, passed through}
 * \event{Other events, passed through}
 * \endevents
 */
template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
auto count_up_to(std::uint64_t threshold, std::uint64_t limit,
                 std::uint64_t initial_count, Downstream &&downstream) {
    return internal::count_up_to<TickEvent, FireEvent, ResetEvent,
                                 FireAfterTick, Downstream>(
        threshold, limit, initial_count, std::forward<Downstream>(downstream));
}

/**
 * \brief Like \ref count_up_to, but decrement the count on each tick event.
 *
 * \ingroup processors-timing
 *
 * All behavior is symmetric to \c count_up_to. \e limit must be less than \e
 * initial_count.
 *
 * \see count_up_to
 */
template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
auto count_down_to(std::uint64_t threshold, std::uint64_t limit,
                   std::uint64_t initial_count, Downstream &&downstream) {
    // Alter parameters to emulate count down using count up.
    if (limit >= initial_count)
        throw std::invalid_argument(
            "count_down_to limit must be less than initial_count");
    if (threshold > initial_count || threshold < limit) {
        // Counter will never fire; no change to threshold needed.
    } else {
        // Mirror threshold around midpoint of initial_count and limit.
        threshold = limit + (initial_count - threshold);
    }
    using std::swap;
    swap(initial_count, limit);

    return internal::count_up_to<TickEvent, FireEvent, ResetEvent,
                                 FireAfterTick, Downstream>(
        threshold, limit, initial_count, std::forward<Downstream>(downstream));
}

} // namespace tcspc
