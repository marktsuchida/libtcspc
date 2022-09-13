/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

namespace flimevt {

namespace internal {

template <typename EIn, typename EReset, typename EOut, bool EmitAfter,
          typename D>
class count_event {
    std::uint64_t count = 0;
    std::uint64_t const thresh;
    std::uint64_t const limit;

    D downstream;

  public:
    explicit count_event(std::uint64_t threshold, std::uint64_t limit,
                         D &&downstream)
        : thresh(threshold), limit(limit), downstream(std::move(downstream)) {
        assert(limit > 0);
    }

    void handle_event(EIn const &event) noexcept {
        if constexpr (!EmitAfter) {
            if (count == thresh)
                downstream.handle_event(EOut{event.macrotime});
        }

        downstream.handle_event(event);
        ++count;

        if constexpr (EmitAfter) {
            if (count == thresh)
                downstream.handle_event(EOut{event.macrotime});
        }

        if (count == limit)
            count = 0;
    }

    void handle_event(EReset const &event) noexcept {
        count = 0;
        downstream.handle_event(event);
    }

    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that counts a specific event and emits an event
 * when the count reaches a threshold.
 *
 * All events (including \c EIn and \c EReset) are passed through.
 *
 * \c EIn and \c EOut must have a macrotime field and \c EOut must be
 * brace-initializable with macrotime (as in \c EOut{123} ).
 *
 * The count is incremented as \c EIn is passed through. Just before or after
 * that (depending on whether \c EmitAfter is false or true), the count is
 * compared to the \e threshold and if equal, \c EOut is emitted, with its
 * macrotime set equal to the \c EIn that triggered it.
 *
 * After incrementing the count and processing the threshold, if the count
 * equals the \e limit, then the count is reset to zero. Automatic resetting
 * can be disabled by setting the limit to \c std::uint64_t{-1} .
 *
 * The \e limit must be positive (a zero limit would imply automatically
 * resetting without any input, which doesn't make sense). When \c EmitAfter is
 * false, \e threshold should be less than the limit; otherwise \c EOut is
 * never emitted. When \c EmitAfter is true, \c threshold should be greater
 * than zero and less than or equal to the limit; otherwise \c EOut is never
 * emitted.
 *
 * When an \c EReset is received (and passed through), the count is reset to
 * zero. No \c EOut is emitted on reset, but if \c EmitAfter is false and the
 * threshold is set to zero, then an \c EOut is emitted on the next \c EIn
 * received.
 *
 * \tparam EIn the event type to count
 * \tparam EReset an event type that causes the count to be reset to zero
 * \tparam EOut the event type to emit when the count reaches the threshold
 * \tparam EmitAfter whether to emit \c EOut after passing through \c EIn
 * \tparam D downstream processor type
 * \param threshold the count value at which to emit \c EOut
 * \param limit the count value at which to reset to zero (set to \c
 * std::uint64_t{-1} if automatic reset is not desired); must be positive
 * \param downstream downstream processor (moved out)
 * \return count-event processor
 */
template <typename EIn, typename EReset, typename EOut, bool EmitAfter,
          typename D>
auto count_event(std::uint64_t threshold, std::uint64_t limit,
                 D &&downstream) {
    return internal::count_event<EIn, EReset, EOut, EmitAfter, D>(
        threshold, limit, std::forward<D>(downstream));
}

} // namespace flimevt
