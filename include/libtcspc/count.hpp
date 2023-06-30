/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
class count_event {
    std::uint64_t count = 0;
    std::uint64_t thresh;
    std::uint64_t limit;

    Downstream downstream;

  public:
    explicit count_event(std::uint64_t threshold, std::uint64_t limit,
                         Downstream &&downstream)
        : thresh(threshold), limit(limit), downstream(std::move(downstream)) {
        assert(limit > 0);
    }

    void handle_event(TickEvent const &event) noexcept {
        if constexpr (!FireAfterTick) {
            if (count == thresh)
                downstream.handle_event(FireEvent{event.macrotime});
        }

        downstream.handle_event(event);
        ++count;

        if constexpr (FireAfterTick) {
            if (count == thresh)
                downstream.handle_event(FireEvent{event.macrotime});
        }

        if (count == limit)
            count = 0;
    }

    void handle_event(ResetEvent const &event) noexcept {
        count = 0;
        downstream.handle_event(event);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that counts a specific event and emits an event
 * when the count reaches a threshold.
 *
 * \ingroup processors
 *
 * All events (including \c TickEvent and \c ResetEvent) are passed through.
 *
 * \c TickEvent and \c FireEvent must have a macrotime field and \c FireEvent
 * must be brace-initializable with macrotime (as in \c FireEvent{123} ).
 *
 * The count is incremented as \c TickEvent is passed through. Just before or
 * after that (depending on whether \c FireAfterTick is false or true), the
 * count is compared to the \e threshold and if equal, \c FireEvent is emitted,
 * with its macrotime set equal to the \c TickEvent that triggered it.
 *
 * After incrementing the count and processing the threshold, if the count
 * equals the \e limit, then the count is reset to zero. Automatic resetting
 * can be disabled by setting the limit to \c std::uint64_t{-1} .
 *
 * The \e limit must be positive (a zero limit would imply automatically
 * resetting without any input, which doesn't make sense). When \c
 * FireAfterTick is false, \e threshold should be less than the limit;
 * otherwise \c FireEvent is never emitted. When \c FireAfterTick is true, \c
 * threshold should be greater than zero and less than or equal to the limit;
 * otherwise \c FireEvent is never emitted.
 *
 * When an \c ResetEvent is received (and passed through), the count is reset
 * to zero. No \c FireEvent is emitted on reset, but if \c FireAfterTick is
 * false and the threshold is set to zero, then an \c FireEvent is emitted on
 * the next \c TickEvent received.
 *
 * \tparam TickEvent the event type to count
 *
 * \tparam FireEvent the event type to emit when the count reaches the
 * threshold
 *
 * \tparam ResetEvent an event type that causes the count to be reset to zero
 *
 * \tparam FireAfterTick whether to emit \c FireEvent after passing through \c
 * TickEvent
 *
 * \tparam Downstream downstream processor type
 *
 * \param threshold the count value at which to emit \c FireEvent
 *
 * \param limit the count value at which to reset to zero (set to \c
 * std::uint64_t{-1} if automatic reset is not desired); must be positive
 *
 * \param downstream downstream processor (moved out)
 *
 * \return count-event processor
 */
template <typename TickEvent, typename FireEvent, typename ResetEvent,
          bool FireAfterTick, typename Downstream>
auto count_event(std::uint64_t threshold, std::uint64_t limit,
                 Downstream &&downstream) {
    return internal::count_event<TickEvent, FireEvent, ResetEvent,
                                 FireAfterTick, Downstream>(
        threshold, limit, std::forward<Downstream>(downstream));
}

} // namespace tcspc
