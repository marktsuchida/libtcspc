/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// Internal implementation of merge processor. This processor is owned by the
// two input processors via shared_ptr.
template <typename EventSet, typename DataTraits, typename Downstream>
class merge_impl {
    // When events have equal abstime, those originating from input 0 are
    // emitted before those originating from input1. Within the same input, the
    // order is preserved.
    // As long as we follow that rule and also ensure never to buffer events
    // that can be emitted, we only ever need to buffer events from one or the
    // other input at any given time.
    bool pending_on_1 = false; // Pending on input 0 if false
    std::array<bool, 2> input_flushed{false, false};
    bool ended_with_exception = false;
    vector_queue<event_variant<EventSet>> pending;
    typename DataTraits::abstime_type max_time_shift;

    Downstream downstream;

    template <unsigned InputChannel>
    [[nodiscard]] auto is_other_flushed() const noexcept -> bool {
        return input_flushed[1 - InputChannel];
    }

    template <unsigned InputChannel>
    [[nodiscard]] auto is_pending_on_other() const noexcept -> bool {
        return pending_on_1 == (InputChannel == 0);
    }

    template <unsigned InputChannel> void set_pending_on() noexcept {
        pending_on_1 = (InputChannel == 1);
    }

    // Emit pending while predicate is true.
    // Pred: bool(abstime_type const &)
    template <typename Pred> void emit_pending(Pred predicate) {
        while (!pending.empty() && std::visit(
                                       [&](auto const &e) {
                                           bool p = predicate(e.abstime);
                                           if (p)
                                               downstream.handle(e);
                                           return p;
                                       },
                                       pending.front()))
            pending.pop();
    }

  public:
    explicit merge_impl(typename DataTraits::abstime_type max_time_shift,
                        Downstream &&downstream)
        : max_time_shift(max_time_shift), downstream(std::move(downstream)) {
        if (max_time_shift < 0)
            throw std::invalid_argument(
                "merge max_time_shift must not be negative");
    }

    merge_impl(merge_impl const &) = delete;
    auto operator=(merge_impl const &) = delete;
    merge_impl(merge_impl &&) = delete;
    auto operator=(merge_impl &&) = delete;
    ~merge_impl() = default;

    template <unsigned InputChannel, typename Event>
    void handle(Event const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTraits::abstime_type>);
        if (ended_with_exception)
            return;
        try {
            if (is_pending_on_other<InputChannel>()) {
                // Emit any older events pending on the other input.
                auto cutoff = event.abstime;
                // Emit events from input 0 before events from input 1 when
                // they have equal abstime.
                if constexpr (InputChannel == 0)
                    --cutoff;
                emit_pending([=](auto t) { return t <= cutoff; });

                // If events still pending on the other input, they are newer
                // (or not older), so we can emit the current event first.
                if (!pending.empty())
                    return downstream.handle(event);

                // If we are still here, we have no more events pending from
                // the other input, but will now enqueue the current event on
                // this input.
                set_pending_on<InputChannel>();
            }
            // If we got here, no events from the other input are pending. If
            // the other input is also flushed, we have no need to buffer.
            if (is_other_flushed<InputChannel>()) {
                assert(pending.empty());
                return downstream.handle(event);
            }

            // Emit any events on the same input if they are older than the
            // maximum allowed time shift between the inputs. Guard against
            // integer underflow.
            constexpr auto abstime_min =
                std::numeric_limits<typename DataTraits::abstime_type>::min();
            if (event.abstime >= 0 ||
                max_time_shift > event.abstime - abstime_min) {
                typename DataTraits::abstime_type old_enough =
                    event.abstime - max_time_shift;
                emit_pending([=](auto t) { return t < old_enough; });
            }

            pending.push(event);
        } catch (std::exception const &) {
            ended_with_exception = true;
            throw;
        }
    }

    template <unsigned InputChannel> void flush() {
        input_flushed[InputChannel] = true;
        if (ended_with_exception)
            return;
        if (is_other_flushed<InputChannel>()) {
            // Since the other input was flushed, events on this input have not
            // been buffered. But there may still be events pending on the
            // other input.
            emit_pending([]([[maybe_unused]] auto t) { return true; });
            downstream.flush();
        } else if (is_pending_on_other<InputChannel>()) {
            // Since this input won't have any more events, no need to buffer
            // the other any more.
            emit_pending([]([[maybe_unused]] auto t) { return true; });
        }
    }
};

template <unsigned InputChannel, typename EventSet, typename DataTraits,
          typename Downstream>
class merge_input {
    std::shared_ptr<merge_impl<EventSet, DataTraits, Downstream>> impl;

  public:
    explicit merge_input(
        std::shared_ptr<merge_impl<EventSet, DataTraits, Downstream>> impl)
        : impl(std::move(impl)) {}

    // Movable but not copyable
    merge_input(merge_input const &) = delete;
    auto operator=(merge_input const &) = delete;
    merge_input(merge_input &&) noexcept = default;
    auto operator=(merge_input &&) noexcept -> merge_input & = default;
    ~merge_input() = default;

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void handle(Event const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTraits::abstime_type>);
        impl->template handle<InputChannel>(event);
    }

    void flush() { impl->template flush<InputChannel>(); }
};

} // namespace internal

/**
 * \brief Create a processor that merges two event streams.
 *
 * \ingroup processors-basic
 *
 * The merged stream will be produced in increasing abstime order, provided
 * that the two input streams have events in increasing abstime order and the
 * time shift between them does not exceed max_time_shift.
 *
 * \see merge_n
 *
 * \tparam EventSet the event set handled by the merge processor
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param max_time_shift the maximum time shift between the two input streams
 *
 * \param downstream downstream processor (will be moved out)
 *
 * \return tuple-like of two processors serving as the input to merge
 */
template <typename EventSet, typename DataTraits = default_data_traits,
          typename Downstream>
auto merge(typename DataTraits::abstime_type max_time_shift,
           Downstream &&downstream) {
    auto p = std::make_shared<
        internal::merge_impl<EventSet, DataTraits, Downstream>>(
        max_time_shift, std::forward<Downstream>(downstream));
    return std::make_pair(
        internal::merge_input<0, EventSet, DataTraits, Downstream>(p),
        internal::merge_input<1, EventSet, DataTraits, Downstream>(p));
}

/**
 * \brief Create a processor that merges a given number of event streams.
 *
 * \ingroup processors-basic
 *
 * The merged stream will be produced in increasing abstime order, provided
 * that all input streams have events in increasing abstime order and the time
 * shift between them does not exceed max_time_shift.
 *
 * This is useful when merging a (compile-time) variable number of similar
 * streams. If the streams to be merged are dissimilar (have different pairwise
 * time shift and especially event frequency), it may be more efficient to
 * manually build a tree using the 2-way \ref merge such that the streams with
 * less frequent events are merged together first.
 *
 * \see merge
 *
 * \tparam N number of input streams
 *
 * \tparam EventSet the event set handled by the merge processor
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param max_time_shift the maximum time shift between the two input streams
 *
 * \param downstream downstream processor
 *
 * \return tuple-like of N processors serving as the input to merge
 */
template <std::size_t N, typename EventSet,
          typename DataTraits = default_data_traits, typename Downstream>
auto merge_n(typename DataTraits::abstime_type max_time_shift,
             Downstream &&downstream) {
    if constexpr (N == 0) {
        return std::tuple{};
    } else if constexpr (N == 1) {
        return std::tuple{std::forward<Downstream>(downstream)};
    } else {
        auto [final_in0, final_in1] = merge<EventSet, DataTraits>(
            max_time_shift, std::forward<Downstream>(downstream));

        std::size_t const left = N / 2;
        std::size_t const right = N - left;
        if constexpr (left == 1) {
            if constexpr (right == 1) {
                return std::tuple{std::move(final_in0), std::move(final_in1)};
            } else {
                return std::tuple_cat(
                    std::tuple{std::move(final_in0)},
                    merge_n<right, EventSet, DataTraits>(
                        max_time_shift, std::move(final_in1)));
            }
        } else {
            return std::tuple_cat(merge_n<left, EventSet, DataTraits>(
                                      max_time_shift, std::move(final_in0)),
                                  merge_n<right, EventSet, DataTraits>(
                                      max_time_shift, std::move(final_in1)));
        }
    }
}

namespace internal {

// Internal implementation of N-way unsorted merge processor. This processor is
// owned by the N input processors via shared_ptr.
template <std::size_t N, typename Downstream> class merge_unsorted_impl {
    Downstream downstream;

    // Cold data.
    bool ended_with_exception = false;
    std::array<bool, N> input_flushed{};

  public:
    explicit merge_unsorted_impl(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    merge_unsorted_impl(merge_unsorted_impl const &) = delete;
    auto operator=(merge_unsorted_impl const &) = delete;
    merge_unsorted_impl(merge_unsorted_impl &&) = delete;
    auto operator=(merge_unsorted_impl &&) = delete;
    ~merge_unsorted_impl() = default;

    template <typename Event> void handle(Event const &event) {
        if (ended_with_exception)
            return;
        try {
            downstream.handle(event);
        } catch (std::exception const &) {
            ended_with_exception = true;
            throw;
        }
    }

    void flush(std::size_t input_channel) {
        input_flushed[input_channel] = true;
        if (ended_with_exception)
            return;
        if (std::all_of(input_flushed.begin(), input_flushed.end(),
                        [](auto f) { return f; }))
            downstream.flush();
    }
};

template <std::size_t N, typename Downstream> class merge_unsorted_input {
    std::shared_ptr<merge_unsorted_impl<N, Downstream>> impl;

    // Cold data.
    std::size_t chan;

  public:
    explicit merge_unsorted_input(
        std::shared_ptr<merge_unsorted_impl<N, Downstream>> impl,
        std::size_t channel)
        : impl(std::move(impl)), chan(channel) {}

    // Movable but not copyable
    merge_unsorted_input(merge_unsorted_input const &) = delete;
    auto operator=(merge_unsorted_input const &) = delete;
    merge_unsorted_input(merge_unsorted_input &&) = default;
    auto operator=(merge_unsorted_input &&) noexcept
        -> merge_unsorted_input & = default;
    ~merge_unsorted_input() = default;

    template <typename Event> void handle(Event const &event) {
        impl->handle(event);
    }

    void flush() { impl->flush(chan); }
};

template <std::size_t N, typename Downstream, std::size_t... Indices>
auto make_merge_unsorted_inputs(
    std::shared_ptr<merge_unsorted_impl<N, Downstream>> impl,
    std::index_sequence<Indices...>) {
    using input_type = merge_unsorted_input<N, Downstream>;
    return std::array<input_type, N>{(input_type(impl, Indices))...};
}

} // namespace internal

/**
 * \brief Create a processor that merges a given number of event streams
 * without sorting by abstime.
 *
 * \ingroup processors-basic
 *
 * The merged stream will handle events in the temporal order they are passed
 * from the upstreams.
 *
 * This is useful when the events on the input streams are known to arrive in
 * the correct order.
 *
 * \see merge
 * \see merge_n
 *
 * \tparam N number of input streams
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return std::array of N processors serving as the input to merge
 */
template <std::size_t N = 2, typename Downstream>
auto merge_n_unsorted(Downstream &&downstream) {
    auto impl = std::make_shared<internal::merge_unsorted_impl<N, Downstream>>(
        std::forward<Downstream>(downstream));
    return internal::make_merge_unsorted_inputs(impl,
                                                std::make_index_sequence<N>());
}

} // namespace tcspc
