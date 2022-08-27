#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

#include <exception>
#include <queue>
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

    // Precondition: pending is not empty
    Macrotime FrontMacrotime() const noexcept {
        return std::visit([](auto const &e) { return e.macrotime; },
                          pending.front());
    }

    // Precondition: pending is not empty
    void EmitFront() noexcept {
        std::visit([&](auto const &e) { downstream.HandleEvent(e); },
                   pending.front());
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

        if (pendingOn1 == (Ch == 0)) {
            // Emit any older events pending on the other input.
            Macrotime cutoff = event.macrotime;
            // Emit events from input 0 before events from input 1 when they
            // have equal macrotime.
            if constexpr (Ch == 0)
                --cutoff;
            while (!pending.empty() && FrontMacrotime() <= cutoff)
                EmitFront();

            // If events still pending on the other input, they are newer (or
            // not older), so we can emit the current event first.
            if (!pending.empty()) {
                downstream.HandleEvent(event);
                return;
            }

            // If we are still here, we have no more events pending from the
            // other input, but will now enqueue the current event on this
            // input.
            pendingOn1 = (Ch == 1);
        }

        // Emit any events on the same input if they are older than the maximum
        // allowed time shift between the inputs.
        // TODO Guard against underflow
        Macrotime oldEnough = event.macrotime - maxTimeShift;
        while (!pending.empty() && FrontMacrotime() < oldEnough)
            EmitFront();

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
            if (!error && otherInputEnded) {
                while (!pending.empty())
                    EmitFront();
            }

            downstream.HandleEnd(error);
        }
    }
};

} // namespace internal

template <unsigned Ch, typename ESet, typename D> class MergeInput;

template <typename ESet, typename D>
auto MakeMerge(Macrotime maxTimeShift, D &&downstream)
    -> std::pair<MergeInput<0, ESet, D>, MergeInput<1, ESet, D>>;

template <unsigned Ch, typename ESet, typename D> class MergeInput {
    std::shared_ptr<internal::MergeImpl<ESet, D>> impl;

    explicit MergeInput(std::shared_ptr<internal::MergeImpl<ESet, D>> impl)
        : impl(impl) {}

    friend auto MakeMerge<ESet, D>(Macrotime maxTimeShift, D &&downstream)
        -> std::pair<MergeInput<0, ESet, D>, MergeInput<1, ESet, D>>;

  public:
    MergeInput(MergeInput const &) = delete;
    MergeInput(MergeInput &&) = default;
    MergeInput &operator=(MergeInput const &) = delete;
    MergeInput &operator=(MergeInput &&) = default;

    template <typename E> void HandleEvent(E const &event) noexcept {
        impl->template HandleEvent<Ch>(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        impl->template HandleEnd<Ch>(error);
        impl.reset();
    }
};

template <typename ESet, typename D>
auto MakeMerge(Macrotime maxTimeShift, D &&downstream)
    -> std::pair<MergeInput<0, ESet, D>, MergeInput<1, ESet, D>> {
    auto p = std::make_shared<internal::MergeImpl<ESet, D>>(
        maxTimeShift, std::move(downstream));
    return std::make_pair(MergeInput<0, ESet, D>(p),
                          MergeInput<1, ESet, D>(p));
}

} // namespace flimevt