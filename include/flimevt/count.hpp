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

template <typename ETick, typename EFire, typename EReset, bool EmitAfter,
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

    void handle_event(ETick const &event) noexcept {
        if constexpr (!EmitAfter) {
            if (count == thresh)
                downstream.handle_event(EFire{event.macrotime});
        }

        downstream.handle_event(event);
        ++count;

        if constexpr (EmitAfter) {
            if (count == thresh)
                downstream.handle_event(EFire{event.macrotime});
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
 * All events (including \c ETick and \c EReset) are passed through.
 *
 * \c ETick and \c EFire must have a macrotime field and \c EFire must be
 * brace-initializable with macrotime (as in \c EFire{123} ).
 *
 * The count is incremented as \c ETick is passed through. Just before or after
 * that (depending on whether \c EmitAfter is false or true), the count is
 * compared to the \e threshold and if equal, \c EFire is emitted, with its
 * macrotime set equal to the \c ETick that triggered it.
 *
 * After incrementing the count and processing the threshold, if the count
 * equals the \e limit, then the count is reset to zero. Automatic resetting
 * can be disabled by setting the limit to \c std::uint64_t{-1} .
 *
 * The \e limit must be positive (a zero limit would imply automatically
 * resetting without any input, which doesn't make sense). When \c EmitAfter is
 * false, \e threshold should be less than the limit; otherwise \c EFire is
 * never emitted. When \c EmitAfter is true, \c threshold should be greater
 * than zero and less than or equal to the limit; otherwise \c EFire is never
 * emitted.
 *
 * When an \c EReset is received (and passed through), the count is reset to
 * zero. No \c EFire is emitted on reset, but if \c EmitAfter is false and the
 * threshold is set to zero, then an \c EFire is emitted on the next \c ETick
 * received.
 *
 * \tparam ETick the event type to count
 * \tparam EFire the event type to emit when the count reaches the threshold
 * \tparam EReset an event type that causes the count to be reset to zero
 * \tparam EmitAfter whether to emit \c EFire after passing through \c ETick
 * \tparam D downstream processor type
 * \param threshold the count value at which to emit \c EFire
 * \param limit the count value at which to reset to zero (set to \c
 * std::uint64_t{-1} if automatic reset is not desired); must be positive
 * \param downstream downstream processor (moved out)
 * \return count-event processor
 */
template <typename ETick, typename EFire, typename EReset, bool EmitAfter,
          typename D>
auto count_event(std::uint64_t threshold, std::uint64_t limit,
                 D &&downstream) {
    return internal::count_event<ETick, EFire, EReset, EmitAfter, D>(
        threshold, limit, std::forward<D>(downstream));
}

} // namespace flimevt
