/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "vector_queue.hpp"

#include <array>
#include <exception>
#include <limits>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// Internal implementation of merge processor. This processor is owned by the
// two input processors via shared_ptr.
template <typename EventSet, typename Downstream> class merge_impl {
    // When events have equal macrotime, those originating from input 0 are
    // emitted before those originating from input1. Within the same input, the
    // order is preserved.
    // As long as we follow that rule and also ensure never to buffer events
    // that can be emitted, we only ever need to buffer events from one or the
    // other input at any given time.
    bool pending_on_1 = false; // Pending on input 0 if false
    std::array<bool, 2> input_ended{false, false};
    bool ended_with_error = false;
    vector_queue<event_variant<EventSet>> pending;
    macrotime max_time_shift;

    Downstream downstream;

    template <unsigned InputChannel>
    [[nodiscard]] auto is_pending_on_other() const noexcept -> bool {
        return pending_on_1 == (InputChannel == 0);
    }

    template <unsigned InputChannel> void set_pending_on() noexcept {
        pending_on_1 = (InputChannel == 1);
    }

    // Emit pending while predicate is true.
    // Pred: bool(macrotime const &)
    template <typename Pred> void emit_pending(Pred predicate) noexcept {
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
    explicit merge_impl(macrotime max_time_shift, Downstream &&downstream)
        : max_time_shift(max_time_shift), downstream(std::move(downstream)) {
        assert(max_time_shift >= 0);
    }

    merge_impl(merge_impl const &) = delete;
    auto operator=(merge_impl const &) = delete;
    merge_impl(merge_impl &&) = delete;
    auto operator=(merge_impl &&) = delete;
    ~merge_impl() = default;

    template <unsigned InputChannel, typename Event>
    void handle_event(Event const &event) noexcept {
        if (ended_with_error)
            return;

        if (is_pending_on_other<InputChannel>()) {
            // Emit any older events pending on the other input.
            macrotime cutoff = event.macrotime;
            // Emit events from input 0 before events from input 1 when they
            // have equal macrotime.
            if constexpr (InputChannel == 0)
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
            set_pending_on<InputChannel>();
        }

        // Emit any events on the same input if they are older than the maximum
        // allowed time shift between the inputs.
        // Guard against integer underflow.
        constexpr auto macrotime_min = std::numeric_limits<macrotime>::min();
        if (event.macrotime >= 0 ||
            max_time_shift > event.macrotime - macrotime_min) {
            macrotime old_enough = event.macrotime - max_time_shift;
            emit_pending([=](auto t) { return t < old_enough; });
        }

        pending.push(event);
    }

    template <unsigned InputChannel>
    void handle_end(std::exception_ptr const &error) noexcept {
        input_ended[InputChannel] = true;
        if (ended_with_error)
            return;

        if (error)
            ended_with_error = true;
        else if (is_pending_on_other<InputChannel>())
            emit_pending([]([[maybe_unused]] auto t) { return true; });

        bool other_input_ended = input_ended[1 - InputChannel];
        if (other_input_ended || error) { // The output stream has ended now.
            {
                // Release queue memory.
                decltype(pending) q;
                pending.swap(q);
            }

            downstream.handle_end(error);
        }
    }
};

template <unsigned InputChannel, typename EventSet, typename Downstream>
class merge_input {
    std::shared_ptr<merge_impl<EventSet, Downstream>> impl;

  public:
    explicit merge_input(
        std::shared_ptr<merge_impl<EventSet, Downstream>> impl)
        : impl(impl) {}

    // Movable but not copyable
    merge_input(merge_input const &) = delete;
    auto operator=(merge_input const &) = delete;
    merge_input(merge_input &&) noexcept = default;
    auto operator=(merge_input &&) noexcept -> merge_input & = default;
    ~merge_input() = default;

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void handle_event(Event const &event) noexcept {
        impl->template handle_event<InputChannel>(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        impl->template handle_end<InputChannel>(error);
        impl.reset();
    }
};

} // namespace internal

/**
 * \brief Create a processor that merges two event streams.
 *
 * \ingroup processors
 *
 * The merged stream will be produced in increasing macrotime order, provided
 * that the two input streams have events in increasing macrotime order and the
 * time shift between them does not exceed max_time_shift.
 *
 * \tparam EventSet the event set handled by the merge processor
 *
 * \tparam Downstream downstream processor type
 *
 * \param max_time_shift the maximum time shift between the two input streams
 *
 * \param downstream downstream processor (will be moved out)
 *
 * \return std::pair of the two processors serving as the input to merge
 */
template <typename EventSet, typename Downstream>
auto merge(macrotime max_time_shift, Downstream &&downstream) {
    auto p = std::make_shared<internal::merge_impl<EventSet, Downstream>>(
        max_time_shift, std::forward<Downstream>(downstream));
    return std::make_pair(internal::merge_input<0, EventSet, Downstream>(p),
                          internal::merge_input<1, EventSet, Downstream>(p));
}

} // namespace tcspc
