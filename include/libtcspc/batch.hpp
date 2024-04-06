/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "introspect.hpp"
#include "object_pool.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename Event, typename EventVector, typename Downstream>
class batch {
    std::shared_ptr<object_pool<EventVector>> buffer_pool;
    std::size_t batch_size;

    std::shared_ptr<EventVector> cur_batch;

    Downstream downstream;

  public:
    explicit batch(std::shared_ptr<object_pool<EventVector>> buffer_pool,
                   std::size_t batch_size, Downstream downstream)
        : buffer_pool(std::move(buffer_pool)), batch_size(batch_size),
          downstream(std::move(downstream)) {
        if (batch_size == 0)
            throw std::invalid_argument(
                "batch processor batch_size must not be zero");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "batch");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Event const &event) {
        if (not cur_batch) {
            cur_batch = buffer_pool->check_out();
            cur_batch->reserve(batch_size);
            cur_batch->clear();
        }

        cur_batch->push_back(event);

        if (cur_batch->size() == batch_size) {
            downstream.handle(cur_batch);
            cur_batch.reset();
        }
    }

    void flush() {
        if (cur_batch && not cur_batch->empty())
            downstream.handle(std::move(cur_batch));
        downstream.flush();
    }
};

template <typename Event, typename Downstream> class unbatch {
    Downstream downstream;

  public:
    explicit unbatch(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "unbatch");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    // Should we mark this LIBTCSPC_NOINLINE? It would be good to increase the
    // chances that the downstream call will be inlined. But preliminary tests
    // (Apple clang 14 arm64) suggest that when the downstream is simple enough
    // to inline, it will be inlined, together with this loop, into upstream;
    // conversely, if the downstream is too complex to inline, it won't be
    // inlined even if this function is marked noinline. There may be
    // borderline cases where this doesn't hold, but it is probably best to
    // leave it to the compiler.
    template <typename EventContainer>
    void handle(EventContainer const &events) {
        for (auto const &event : events)
            downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that batches events into vectors for buffering.
 *
 * \ingroup processors-basic
 *
 * Collects every \e batch_size events into vectors. The vectors are obtained
 * from the given \e buffer_pool and are passed downstream via \c
 * std::shared_ptr.
 *
 * This is not quite symmetric with \ref unbatch, which handles bare event
 * containers.
 *
 * This processor does not perform time-based batching, so may introduce
 * arbitrary delays to real-time event streams. For this reason, batching
 * should not be performed (and is not necessary) for intermediate buffering of
 * real-time streams in most cases. Batching is useful for writing to files
 * efficiently.
 *
 * In contrast, streams originating from a stored source (e.g., a file) would
 * cause buffers to grow unbounded if not regulated. In this case, buffers
 * should use batching, with a size-limited \e buffer_pool so that buffer sizes
 * are bounded.
 *
 * \see unbatch
 *
 * \tparam Event the event type (must be a trivial type)
 *
 * \tparam EventVector vector-like container of events
 *
 * \tparam Downstream downstream processor type
 *
 * \param buffer_pool object pool providing event vectors
 *
 * \param batch_size number of events to collect in each batch
 *
 * \param downstream downstream processor
 *
 * \return batch processor
 */
template <typename Event, typename EventVector, typename Downstream>
auto batch(std::shared_ptr<object_pool<EventVector>> buffer_pool,
           std::size_t batch_size, Downstream &&downstream) {
    return internal::batch<Event, EventVector, Downstream>(
        std::move(buffer_pool), batch_size,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor transforming batches of events to individual
 * events.
 *
 * \ingroup processors-basic
 *
 * Events in (ordered) containers or spans are emitted one by one.
 *
 * This is not quite symmetric with \ref batch, which requires the container to
 * be vector-like, and emits them via \c std::shared_ptr.
 *
 * \see batch
 *
 * \see dereference_pointer
 *
 * \tparam Event the event type (must be a trivial type)
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return unbatch processor
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
 * \ingroup processors-basic
 *
 * This is intended for use in cases where separating the processing loop is
 * beneficial, for example to limit the (code or data) working set size.
 * Usually the regular \c buffer (requiring two separate threads) is more
 * beneficial because it can exploit parallellism, but a single-threaded buffer
 * is easier to introduce (it can simply be inserted in a processor chain) so
 * may be convenient for experimentation.
 *
 * Events are buffered until \p batch_size is reached, without regard to
 * timing, so this type of buffer is usually not appropriate for live
 * processing.
 *
 * \see buffer
 */
template <typename Event, typename Downstream>
auto process_in_batches(std::size_t batch_size, Downstream &&downstream) {
    using event_vector = std::vector<Event>;
    return batch<Event, event_vector>(
        std::make_shared<object_pool<event_vector>>(0, 1), batch_size,
        dereference_pointer<std::shared_ptr<event_vector>>(
            unbatch<Event>(std::forward<Downstream>(downstream))));
}

} // namespace tcspc
