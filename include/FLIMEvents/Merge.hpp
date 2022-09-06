/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

#include <exception>
#include <limits>
#include <queue>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

// Internal implementation of merge processor. This processor is owned by the
// two input processors via shared_ptr.
template <typename ESet, typename D> class MergeImpl {
    // When events have equal macrotime, those originating from input 0 are
    // emitted before those originating from input1. Within the same input, the
    // order is preserved.
    // As long as we follow that rule and also ensure never to buffer events
    // that can be emitted, we only ever need to buffer events from one or the
    // other input at any given time.
    bool pendingOn1 = false; // Pending on input 0 if false
    bool inputEnded[2] = {false, false};
    bool canceled = false; // Received error on one input
    std::queue<EventVariant<ESet>> pending;
    Macrotime const maxTimeShift;

    D downstream;

    template <unsigned C> bool IsPendingOnOther() const noexcept {
        return pendingOn1 == (C == 0);
    }

    template <unsigned C> void SetPendingOn() noexcept {
        pendingOn1 = (C == 1);
    }

    // Emit pending while predicate is true.
    // P: bool(Macrotime const &)
    template <typename P> void EmitPending(P predicate) noexcept {
        while (!pending.empty() && std::visit(
                                       [&](auto const &e) {
                                           bool p = predicate(e.macrotime);
                                           if (p)
                                               downstream.HandleEvent(e);
                                           return p;
                                       },
                                       pending.front()))
            pending.pop();
    }

  public:
    MergeImpl(MergeImpl const &) = delete;
    MergeImpl &operator=(MergeImpl const &) = delete;

    explicit MergeImpl(Macrotime maxTimeShift, D &&downstream)
        : maxTimeShift(maxTimeShift), downstream(std::move(downstream)) {
        assert(maxTimeShift >= 0);
    }

    template <unsigned Ch, typename E>
    void HandleEvent(E const &event) noexcept {
        if (canceled)
            return;

        if (IsPendingOnOther<Ch>()) {
            // Emit any older events pending on the other input.
            Macrotime cutoff = event.macrotime;
            // Emit events from input 0 before events from input 1 when they
            // have equal macrotime.
            if constexpr (Ch == 0)
                --cutoff;
            EmitPending([=](auto t) { return t <= cutoff; });

            // If events still pending on the other input, they are newer (or
            // not older), so we can emit the current event first.
            if (!pending.empty()) {
                downstream.HandleEvent(event);
                return;
            }

            // If we are still here, we have no more events pending from the
            // other input, but will now enqueue the current event on this
            // input.
            SetPendingOn<Ch>();
        }

        // Emit any events on the same input if they are older than the maximum
        // allowed time shift between the inputs.
        // Guard against integer underflow.
        constexpr auto macrotimeMin = std::numeric_limits<Macrotime>::min();
        if (maxTimeShift > event.macrotime - macrotimeMin) {
            Macrotime oldEnough = event.macrotime - maxTimeShift;
            EmitPending([=](auto t) { return t < oldEnough; });
        }

        pending.push(event);
    }

    template <unsigned Ch> void HandleEnd(std::exception_ptr error) noexcept {
        inputEnded[Ch] = true;
        if (canceled)
            return;

        bool otherInputEnded = inputEnded[1 - Ch];
        if (error && !otherInputEnded)
            canceled = true;
        if (otherInputEnded || canceled) {
            // Emit pending events if both streams are complete (had no error).
            if (!error && otherInputEnded)
                EmitPending([]([[maybe_unused]] auto t) { return true; });

            downstream.HandleEnd(error);
        }
    }
};

} // namespace internal

/**
 * \brief Processor proxying input to a merge processor.
 *
 * \see flimevt::MakeMerge()
 *
 * \tparam Ch 0 or 1
 * \tparam ESet the event set handled by the merge processor
 * \tparam D downstream processor type
 */
template <unsigned Ch, typename ESet, typename D> class MergeInput {
    std::shared_ptr<internal::MergeImpl<ESet, D>> impl;

    explicit MergeInput(std::shared_ptr<internal::MergeImpl<ESet, D>> impl)
        : impl(impl) {}

    template <typename MESet, typename MD>
    friend auto MakeMerge(Macrotime maxTimeShift, MD &&downstream);

  public:
    /** \brief Noncopyable */
    MergeInput(MergeInput const &) = delete;
    /** \brief Noncopyable */
    MergeInput &operator=(MergeInput const &) = delete;
    /** \brief Move constructor */
    MergeInput(MergeInput &&) = default;
    /** \brief Move assignment operator */
    MergeInput &operator=(MergeInput &&) = default;

    /** \brief Processor interface */
    template <typename E, typename = std::enable_if_t<ContainsEventV<ESet, E>>>
    void HandleEvent(E const &event) noexcept {
        impl->template HandleEvent<Ch>(event);
    }

    /** \brief Processor interface */
    void HandleEnd(std::exception_ptr error) noexcept {
        impl->template HandleEnd<Ch>(error);
        impl.reset();
    }
};

/**
 * \brief Create a processor that merges two event streams.
 *
 * The merged stream will be produced in increasing macrotime order, provided
 * that the two input streams have events in increasing macrotime order and the
 * time shift between them does not exceed maxTimeShift.
 *
 * \tparam ESet the event set handled by the merge processor
 * \tparam D downstream processor type
 * \param maxTimeShift the maximum time shift between the two input streams
 * \param downstream downstream processor (will be moved out)
 * \return std::pair of MergeInput processors
 */
template <typename ESet, typename D>
auto MakeMerge(Macrotime maxTimeShift, D &&downstream) {
    auto p = std::make_shared<internal::MergeImpl<ESet, D>>(
        maxTimeShift, std::move(downstream));
    return std::make_pair(MergeInput<0, ESet, D>(p),
                          MergeInput<1, ESet, D>(p));
}

} // namespace flimevt
