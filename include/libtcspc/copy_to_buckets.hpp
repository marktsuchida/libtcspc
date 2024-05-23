/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bucket.hpp"
#include "common.hpp"
#include "core.hpp"
#include "errors.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"
#include "span.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename T, typename Downstream> class copy_to_buckets {
    static_assert(is_processor_v<Downstream, bucket<T>>);

    std::shared_ptr<bucket_source<T>> bsource;

    Downstream downstream;

  public:
    explicit copy_to_buckets(std::shared_ptr<bucket_source<T>> buffer_provider,
                             Downstream &&downstream)
        : bsource(std::move(buffer_provider)),
          downstream(std::move(downstream)) {
        if (not bsource)
            throw std::invalid_argument(
                "copy_to_buckets buffer_provider must not be null");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "copy_to_buckets");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename Event,
              typename = std::enable_if_t<
                  std::is_constructible_v<span<T const>, Event> ||
                  handles_event_v<Downstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        if constexpr (std::is_constructible_v<span<T const>, Event>) {
            auto const event_span = span<T const>(event);
            auto b = bsource->bucket_of_size(event_span.size());
            std::copy(event_span.begin(), event_span.end(), b.begin());
            downstream.handle(std::move(b));
        } else {
            downstream.handle(std::forward<Event>(event));
        }
    }

    void flush() { downstream.flush(); }
};

template <typename T, typename LiveDownstream, typename BatchDownstream>
class copy_to_full_buckets {
    static_assert(is_processor_v<LiveDownstream, bucket<T const>>);
    static_assert(is_processor_v<BatchDownstream, bucket<T>>);

    std::shared_ptr<bucket_source<T>> bsource;
    std::size_t bsize;

    bucket<T> bkt;
    std::size_t filled = 0;

    LiveDownstream live_downstream;
    BatchDownstream batch_downstream;

    // Mutates 'b' only when throwing.
    void emit_live(bucket<T> &b, std::size_t start, std::size_t count) {
        if (count > 0) {
            try {
                auto v = bsource->shared_view_of(b);
                v.shrink(start, count);
                live_downstream.handle(std::move(v));
            } catch (end_of_processing const &) {
                b.shrink(0, start + count);
                batch_downstream.handle(std::move(b));
                batch_downstream.flush();
                throw;
            }
        }
    }

    void emit_batch(bucket<T> &&b) {
        try {
            batch_downstream.handle(std::move(b));
        } catch (end_of_processing const &) {
            live_downstream.flush();
            throw;
        }
    }

    void flush_batch() {
        if (not bkt.empty() && filled > 0) {
            bkt.shrink(0, filled);
            batch_downstream.handle(std::move(bkt));
        }
        batch_downstream.flush();
    }

  public:
    explicit copy_to_full_buckets(
        std::shared_ptr<bucket_source<T>> buffer_provider,
        arg::batch_size<std::size_t> batch_size,
        LiveDownstream &&live_downstream, BatchDownstream &&batch_downstream)
        : bsource(std::move(buffer_provider)), bsize(batch_size.value),
          live_downstream(std::move(live_downstream)),
          batch_downstream(std::move(batch_downstream)) {
        if (not bsource)
            throw std::invalid_argument(
                "copy_to_full_buckets buffer_provider must not be null");
        if constexpr (not std::is_same_v<LiveDownstream, null_sink>) {
            if (not bsource->supports_shared_views())
                throw std::invalid_argument(
                    "copy_to_full_buckets buffer_provider must support shared views");
        }
        if (bsize == 0)
            throw std::invalid_argument(
                "copy_to_full_buckets batch size must be positive");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "copy_to_full_buckets");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return merge_processor_graphs(
            live_downstream.introspect_graph().push_entry_point(this),
            batch_downstream.introspect_graph().push_entry_point(this));
    }

    template <typename Event,
              typename = std::enable_if_t<
                  std::is_constructible_v<span<T const>, Event> ||
                  handles_event_v<LiveDownstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        if constexpr (std::is_constructible_v<span<T const>, Event>) {
            auto src = span<T const>(event);
            while (not src.empty()) {
                if (filled == 0 && bkt.empty())
                    bkt = bsource->bucket_of_size(bsize);
                auto const dest = span(bkt).subspan(filled);
                auto const copy_size = std::min(src.size(), dest.size());
                std::copy_n(src.begin(), copy_size, dest.begin());
                if constexpr (not std::is_same_v<LiveDownstream, null_sink>)
                    emit_live(bkt, filled, copy_size);
                filled += copy_size;
                if (filled == bsize) {
                    emit_batch(std::move(bkt));
                    bkt = {};
                    filled = 0;
                }
                src = src.subspan(copy_size);
            }
        } else {
            try {
                live_downstream.handle(std::forward<Event>(event));
            } catch (end_of_processing const &) {
                flush_batch();
                throw;
            }
        }
    }

    void flush() {
        std::exception_ptr end;
        try {
            live_downstream.flush();
        } catch (end_of_processing const &) {
            end = std::current_exception();
        }
        flush_batch();
        if (end)
            std::rethrow_exception(end);
    }
};

} // namespace internal

/**
 * \brief Create a processor that copies batches of data into buckets.
 *
 * \ingroup processors-acquisition
 *
 * This processor is used to integrate a push-style device API (i.e., one in
 * which the driver API calls our callback with acquired data) as a data source
 * for libtcspc that can be buffered.
 *
 * The contents of events explicitly convertible to `span<T const>` are copied
 * to `bucket<T>` (of variable size) obtained from the given \p
 * buffer_provider.
 *
 * Events other than those explicitly convertible to `span<T const>` are passed
 * through. This can be used to transmit out-of-band timing events.
 *
 * \tparam T the element type of the data (usually a byte or integer type)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param buffer_provider bucket source providing event buffers
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Contiguous container or span of `T const`: copy to a `tcspc::bucket<T>` of
 *   matching size and emit
 * - All other types: pass through without action
 * - Flush: pass through without action
 */
template <typename T, typename Downstream>
auto copy_to_buckets(std::shared_ptr<bucket_source<T>> buffer_provider,
                     Downstream &&downstream) {
    return internal::copy_to_buckets<T, Downstream>(
        std::move(buffer_provider), std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that copies data into buckets, ensuring that each
 * bucket is filled to a fixed size but also providing views of partial buckets
 * in real time.
 *
 * \ingroup processors-acquisition
 *
 * This processor is used to integrate a push-style device API (i.e., one in
 * which the driver API calls our callback with acquired data) as a data source
 * for libtcspc that can be buffered.
 *
 * The contents of events explicitly convertible to `span<T const>` are copied
 * to fixed-size buckets.
 *
 * The processor attaches two downstream processors. The \p live_downstream
 * receives newly copied data as soon as it is available, but in the form of a
 * const view bucket. This is typically used for live processing and display.
 *
 * The \p batch_downstream receives the same data, but only as each batch fills
 * up to the given \p batch_size (except for the last batch, which may be
 * smaller). This is typically used for saving the raw data to disk.
 *
 * The two streams share the underlying bucket storage.
 *
 * Events other than those explicitly convertible to `span<T const>` are passed
 * through only to the \p live_downstream. Thus, their order relative to the
 * data batches is preserved. This can be used to transmit out-of-band timing
 * events.
 *
 * \tparam T the element type of the data (usually a byte or integer type)
 *
 * \tparam LiveDownstream type of the "live" downstream processor (usually
 * deduced)
 *
 * \tparam BatchDownstream type of the "batch" downstream processor (usually
 * deduced)
 *
 * \param buffer_provider bucket source providing event buffers (must support
 * shared views unless \p LiveDownstream is `tcspc::null_sink`)
 *
 * \param batch_size number of elements (`T`) to collect in each bucket
 *
 * \param live_downstream downstream processor receiving read-only views of
 * data per each read
 *
 * \param batch_downstream downstream processor receiving full buckets
 *
 * \return processor
 *
 * \par Events handled
 * - Contiguous container or span of `T const`: copy into successive
 *   `tcspc::bucket<T>`s of size `batch_size`, emitting the copied portion to
 *   `live_downstream` as (const) `tcspc::bucket<T const>` and any full buckets
 *   to `batch_downstream` as (rvalue) `tcspc::bucket<T>`.
 * - All other types: pass through without action
 * - Flush: emit any pending non-full bucket to `batch_downstream`; pass
 *   through
 */
template <typename T, typename LiveDownstream, typename BatchDownstream>
auto copy_to_full_buckets(std::shared_ptr<bucket_source<T>> buffer_provider,
                          arg::batch_size<std::size_t> batch_size,
                          LiveDownstream &&live_downstream,
                          BatchDownstream &&batch_downstream) {
    return internal::copy_to_full_buckets<T, LiveDownstream, BatchDownstream>(
        std::move(buffer_provider), batch_size,
        std::forward<LiveDownstream>(live_downstream),
        std::forward<BatchDownstream>(batch_downstream));
}

}; // namespace tcspc
