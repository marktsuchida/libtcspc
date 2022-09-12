/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"

#include <exception>
#include <limits>
#include <queue>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

// Internal implementation of merge processor. This processor is owned by the
// two input processors via shared_ptr.
template <typename Es, typename D> class merge_impl {
    // When events have equal macrotime, those originating from input 0 are
    // emitted before those originating from input1. Within the same input, the
    // order is preserved.
    // As long as we follow that rule and also ensure never to buffer events
    // that can be emitted, we only ever need to buffer events from one or the
    // other input at any given time.
    bool pending_on_1 = false; // Pending on input 0 if false
    bool input_ended[2] = {false, false};
    bool canceled = false; // Received error on one input
    std::queue<event_variant<Es>> pending;
    macrotime const max_time_shift;

    D downstream;

    template <unsigned C> bool is_pending_on_other() const noexcept {
        return pending_on_1 == (C == 0);
    }

    template <unsigned C> void set_pending_on() noexcept {
        pending_on_1 = (C == 1);
    }

    // Emit pending while predicate is true.
    // P: bool(macrotime const &)
    template <typename P> void emit_pending(P predicate) noexcept {
        while (!pending.empty() && std::visit(
                                       [&](auto const &e) {
                                           bool p = predicate(e.macrotime);
                                           if (p)
                                               downstream.handle_event(e);
                                           return p;
                                       },
                                       pending.front()))
            pending.pop();
    }

  public:
    merge_impl(merge_impl const &) = delete;
    merge_impl &operator=(merge_impl const &) = delete;

    explicit merge_impl(macrotime max_time_shift, D &&downstream)
        : max_time_shift(max_time_shift), downstream(std::move(downstream)) {
        assert(max_time_shift >= 0);
    }

    template <unsigned Ch, typename E>
    void handle_event(E const &event) noexcept {
        if (canceled)
            return;

        if (is_pending_on_other<Ch>()) {
            // Emit any older events pending on the other input.
            macrotime cutoff = event.macrotime;
            // Emit events from input 0 before events from input 1 when they
            // have equal macrotime.
            if constexpr (Ch == 0)
                --cutoff;
            emit_pending([=](auto t) { return t <= cutoff; });

            // If events still pending on the other input, they are newer (or
            // not older), so we can emit the current event first.
            if (!pending.empty()) {
                downstream.handle_event(event);
                return;
            }

            // If we are still here, we have no more events pending from the
            // other input, but will now enqueue the current event on this
            // input.
            set_pending_on<Ch>();
        }

        // Emit any events on the same input if they are older than the maximum
        // allowed time shift between the inputs.
        // Guard against integer underflow.
        constexpr auto macrotime_min = std::numeric_limits<macrotime>::min();
        if (max_time_shift > event.macrotime - macrotime_min) {
            macrotime old_enough = event.macrotime - max_time_shift;
            emit_pending([=](auto t) { return t < old_enough; });
        }

        pending.push(event);
    }

    template <unsigned Ch> void handle_end(std::exception_ptr error) noexcept {
        input_ended[Ch] = true;
        if (canceled) // Other input already had an error
            return;

        bool other_input_ended = input_ended[1 - Ch];
        if (other_input_ended && !error) // They had finished; now we did, too
            emit_pending([]([[maybe_unused]] auto t) { return true; });
        if (!other_input_ended && error) // We errored first
            canceled = true;
        if (other_input_ended || error) { // The stream has ended now
            {
                // Release queue memory. We cannot access the std::deque of the
                // std::queue, but we can swap it with an empty one.
                decltype(pending) q;
                pending.swap(q);
            }

            downstream.handle_end(error);
        }
    }
};

} // namespace internal

/**
 * \brief Processor proxying input to a merge processor.
 *
 * \see flimevt::make_merge()
 *
 * \tparam Ch 0 or 1
 * \tparam Es the event set handled by the merge processor
 * \tparam D downstream processor type
 */
template <unsigned Ch, typename Es, typename D> class merge_input {
    std::shared_ptr<internal::merge_impl<Es, D>> impl;

    explicit merge_input(std::shared_ptr<internal::merge_impl<Es, D>> impl)
        : impl(impl) {}

    template <typename MESet, typename MD>
    friend auto make_merge(macrotime max_time_shift, MD &&downstream);

  public:
    /** \brief Noncopyable */
    merge_input(merge_input const &) = delete;
    /** \brief Noncopyable */
    merge_input &operator=(merge_input const &) = delete;
    /** \brief Move constructor */
    merge_input(merge_input &&) = default;
    /** \brief Move assignment operator */
    merge_input &operator=(merge_input &&) = default;

    /** \brief Processor interface */
    template <typename E, typename = std::enable_if_t<contains_event_v<Es, E>>>
    void handle_event(E const &event) noexcept {
        impl->template handle_event<Ch>(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        impl->template handle_end<Ch>(error);
        impl.reset();
    }
};

/**
 * \brief Create a processor that merges two event streams.
 *
 * The merged stream will be produced in increasing macrotime order, provided
 * that the two input streams have events in increasing macrotime order and the
 * time shift between them does not exceed max_time_shift.
 *
 * \tparam Es the event set handled by the merge processor
 * \tparam D downstream processor type
 * \param max_time_shift the maximum time shift between the two input streams
 * \param downstream downstream processor (will be moved out)
 * \return std::pair of merge_input processors
 */
template <typename Es, typename D>
auto make_merge(macrotime max_time_shift, D &&downstream) {
    auto p = std::make_shared<internal::merge_impl<Es, D>>(
        max_time_shift, std::move(downstream));
    return std::make_pair(merge_input<0, Es, D>(p), merge_input<1, Es, D>(p));
}

} // namespace flimevt
