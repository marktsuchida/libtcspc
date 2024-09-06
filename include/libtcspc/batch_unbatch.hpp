/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bucket.hpp"
#include "common.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <cstddef>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Event, typename Downstream> class batch {
    static_assert(is_processor_v<Downstream, bucket<Event>>);

    std::shared_ptr<bucket_source<Event>> bsource;
    std::size_t bsize;

    bucket<Event> cur_bucket;
    std::size_t n_filled = 0;

    Downstream downstream;

  public:
    explicit batch(std::shared_ptr<bucket_source<Event>> buffer_provider,
                   arg::batch_size<std::size_t> batch_size,
                   Downstream downstream)
        : bsource(std::move(buffer_provider)), bsize(batch_size.value),
          downstream(std::move(downstream)) {
        if (bsize == 0)
            throw std::invalid_argument(
                "batch processor batch_size must not be zero");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "batch");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E, typename = std::enable_if_t<
                              std::is_convertible_v<remove_cvref_t<E>, Event>>>
    void handle(E &&event) {
        if (cur_bucket.empty())
            cur_bucket = bsource->bucket_of_size(bsize);

        cur_bucket[n_filled] = std::forward<E>(event);
        ++n_filled;

        if (n_filled == bsize) {
            downstream.handle(std::move(cur_bucket));
            cur_bucket = {};
            n_filled = 0;
        }
    }

    void flush() {
        if (n_filled > 0) {
            cur_bucket.shrink(0, n_filled);
            downstream.handle(std::move(cur_bucket));
        }
        downstream.flush();
    }
};

template <typename Event, typename Downstream> class unbatch {
    static_assert(is_processor_v<Downstream, Event>);

    Downstream downstream;

  public:
    explicit unbatch(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "unbatch");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    // Should we mark this LIBTCSPC_NOINLINE? It would be good to increase the
    // chances that the downstream call will be inlined. But preliminary tests
    // (Apple clang 14 arm64) suggest that when the downstream is simple enough
    // to inline, it will be inlined, together with this loop, into upstream;
    // conversely, if the downstream is too complex to inline, it won't be
    // inlined even if this function is marked noinline. There may be
    // borderline cases where this doesn't hold, but it is probably best to
    // leave it to the compiler.
    template <
        typename EventContainer,
        typename = std::enable_if_t<std::is_convertible_v<
            typename std::iterator_traits<
                decltype(std::declval<EventContainer>().end())>::reference,
            Event const &>>>
    void handle(EventContainer const &events) {
        for (auto const &event : events)
            downstream.handle(event);
    }

    // If the container is passed by rvalue ref and its elements are not const,
    // move out individually. (For what it's worth: it will not make a
    // difference for trivial element types, which is the typical usage.)
    // Note that we do not do a move if the container is not passed by rvalue
    // ref, even if its elements are non-const (e.g., a const span to
    // non-const), because that would lead to surprises.
    template <typename EC,
              typename = std::enable_if_t<
                  not std::is_lvalue_reference_v<EC> &&
                  std::is_convertible_v<
                      typename std::iterator_traits<
                          decltype(std::declval<EC>().end())>::reference,
                      Event &>>>
    void handle(EC &&events) { // NOLINT(cppcoreguidelines-missing-std-forward)
        for (auto &event : events)
            downstream.handle(std::move(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that batches events into buckets for buffering.
 *
 * \ingroup processors-batching
 *
 * Collects every \p batch_size events into a bucket. The buckets are obtained
 * from the given \p buffer_provider.
 *
 * The buckets are emitted as rvalue reference.
 *
 * This processor does not perform time-based batching, so may introduce
 * arbitrary delays to real-time event streams. For this reason, batching
 * should not be performed (and is not necessary) for intermediate buffering of
 * real-time streams in most cases.
 *
 * \see `tcspc::unbatch()`
 *
 * \tparam Event the event type (must be a trivial type)
 *
 * \tparam Downstream downstream processor type
 *
 * \param buffer_provider bucket source providing event buffers
 *
 * \param batch_size number of events to collect in each bucket
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: collected into `tcspc::bucket<Event>` and emitted as batch
 * - Flush: emit any buffered events as `tcspc::bucket<Event>`; pass through
 */
template <typename Event, typename Downstream>
auto batch(std::shared_ptr<bucket_source<Event>> buffer_provider,
           arg::batch_size<std::size_t> batch_size, Downstream &&downstream) {
    return internal::batch<Event, Downstream>(
        std::move(buffer_provider), batch_size,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor transforming batches of events to individual
 * events.
 *
 * \ingroup processors-batching
 *
 * Events in (ordered) containers or spans are emitted one by one.
 *
 * \see `tcspc::batch()`
 *
 * \tparam Event the event type (must be a trivial type)
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Range (container, iterable) of `Event`: each element event emitted in
 *   order
 * - Flush: pass through with no action
 */
template <typename Event, typename Downstream>
auto unbatch(Downstream &&downstream) {
    return internal::unbatch<Event, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that buffers events up to equally sized batches
 * and passes them downstream in a tight loop.
 *
 * \ingroup processors-buffering
 *
 * This is intended for use in cases where separating the processing loop is
 * beneficial, for example to limit the (code or data) working set size.
 * Usually the regular `tcspc::buffer()` (requiring two separate threads) is
 * more beneficial because it can exploit parallellism, but a single-threaded
 * buffer is easier to introduce (it can simply be inserted in a processor
 * graph) so may be convenient for experimentation.
 *
 * Events are buffered until \p batch_size is reached, without regard to
 * timing, so this type of buffer is usually not appropriate for live
 * processing.
 *
 * \see `tcspc::buffer()`
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: buffer up to \p batch_size; then emit all buffered
 * - Flush: emit any buffered events; pass through
 */
template <typename Event, typename Downstream>
auto process_in_batches(arg::batch_size<std::size_t> batch_size,
                        Downstream &&downstream) {
    return batch<Event>(recycling_bucket_source<Event>::create(1), batch_size,
                        unbatch<Event>(std::forward<Downstream>(downstream)));
}

} // namespace tcspc
