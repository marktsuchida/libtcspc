/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bucket.hpp"
#include "context.hpp"
#include "core.hpp"
#include "errors.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"
#include "span.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace tcspc {

/**
 * \brief Access for acquire processors.
 *
 * \ingroup context-access
 */
class acquire_access {
    std::function<void()> halt_fn;

  public:
    /** \private */
    template <typename Func>
    explicit acquire_access(Func halt_func) : halt_fn(halt_func) {}

    /**
     * \brief Halt the acquisition: stop reading further data.
     *
     * The call to `flush()` will throw (if it hasn't exited yet)
     * `tcspc::acquisition_halted` without flushing the downstream.
     *
     * This is typically used for user-initiated cancellation of the
     * acquisition.
     *
     * \note This function does not block; it may return before the acquisition
     * actually stops and the processor's `flush()` returns (or possibly
     * throws). You should wait for the latter before cleaning up resources
     * needed by the acquisition (such as stopping the acquisition at the
     * driver level).
     */
    void halt() { halt_fn(); }
};

namespace internal {

// The max sleep duration when a read doesn't fill a batch is chosen to be
// short enough that (1) its effect is unnoticeable in a live display of the
// data and (2) hardware buffers are unlikely to fill up if they started out
// empty, given the buffer capacity and maximum count rates of typical devices.
constexpr auto slow_acq_sleep = std::chrono::milliseconds(10);

template <typename T, typename Reader, typename Downstream> class acquire {
    static_assert(is_processor_v<Downstream, bucket<T>>);

    Reader reader;
    std::shared_ptr<bucket_source<T>> bsource;
    std::size_t bsize;

    std::mutex halt_mutex;
    std::condition_variable halt_cv;
    bool halted = false;

    Downstream downstream;

    // Cold data after downstream.
    access_tracker<acquire_access> trk;

    void halt() {
        {
            std::scoped_lock lock(halt_mutex);
            halted = true;
        }
        halt_cv.notify_one();
    }

  public:
    explicit acquire(Reader &&reader,
                     std::shared_ptr<bucket_source<T>> buffer_provider,
                     arg::batch_size<std::size_t> batch_size,
                     access_tracker<acquire_access> tracker,
                     Downstream &&downstream)
        : reader(std::move(reader)), bsource(std::move(buffer_provider)),
          bsize(batch_size.value), downstream(std::move(downstream)),
          trk(std::move(tracker)) {
        if (not bsource)
            throw std::invalid_argument(
                "acquire buffer_provider must not be null");
        if (bsize == 0)
            throw std::invalid_argument("acquire batch size must be positive");

        trk.register_access_factory([](auto &tracker) {
            auto *self = LIBTCSPC_OBJECT_FROM_TRACKER(acquire, trk, tracker);
            return acquire_access([self] { self->halt(); });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "acquire");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void flush() {
        bucket<T> b;
        bool reached_end = false;
        {
            std::unique_lock lock(halt_mutex);
            while (not halted) {
                lock.unlock();
                auto const start_time = std::chrono::steady_clock::now();
                if (b.empty())
                    b = bsource->bucket_of_size(bsize);
                std::optional<std::size_t> const read = reader(span(b));
                if (not read) {
                    reached_end = true;
                    break;
                }
                if (*read > 0) {
                    b.shrink(0, *read);
                    downstream.handle(std::move(b));
                    b = {};
                }
                lock.lock();
                if (*read < bsize) { // Not enough data to fill the batch.
                    halt_cv.wait_until(lock, start_time + slow_acq_sleep,
                                       [&] { return halted; });
                }
            }
        }
        if (reached_end)
            downstream.flush();
        else
            throw acquisition_halted();
    }
};

template <typename T, typename Reader, typename LiveDownstream,
          typename BatchDownstream>
class acquire_full_buckets {
    static_assert(is_processor_v<LiveDownstream, bucket<T const>>);
    static_assert(is_processor_v<BatchDownstream, bucket<T>>);

    Reader reader;
    std::shared_ptr<bucket_source<T>> bsource;
    std::size_t bsize;

    std::mutex halt_mutex;
    std::condition_variable halt_cv;
    bool halted = false;

    LiveDownstream live_downstream;
    BatchDownstream batch_downstream;

    // Cold data after downstream.
    access_tracker<acquire_access> trk;

    void halt() {
        {
            std::scoped_lock lock(halt_mutex);
            halted = true;
        }
        halt_cv.notify_one();
    }

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

    void flush_downstreams(bucket<T> &&b, std::size_t filled) {
        std::exception_ptr end;
        try {
            live_downstream.flush();
        } catch (end_of_processing const &) {
            end = std::current_exception();
        }
        if (not b.empty() && filled > 0) {
            b.shrink(0, filled);
            batch_downstream.handle(std::move(b));
        }
        batch_downstream.flush();
        if (end)
            std::rethrow_exception(end);
    }

  public:
    explicit acquire_full_buckets(
        Reader &&reader, std::shared_ptr<bucket_source<T>> buffer_provider,
        arg::batch_size<std::size_t> batch_size,
        access_tracker<acquire_access> tracker,
        LiveDownstream &&live_downstream, BatchDownstream &&batch_downstream)
        : reader(std::move(reader)), bsource(std::move(buffer_provider)),
          bsize(batch_size.value), live_downstream(std::move(live_downstream)),
          batch_downstream(std::move(batch_downstream)),
          trk(std::move(tracker)) {
        if (not bsource)
            throw std::invalid_argument(
                "acquire_full_buckets buffer_provider must not be null");
        if constexpr (not std::is_same_v<LiveDownstream, null_sink>) {
            if (not bsource->supports_shared_views())
                throw std::invalid_argument(
                    "acquire_full_buckets buffer_provider must support shared views");
        }
        if (bsize == 0)
            throw std::invalid_argument(
                "acquire_full_buckets batch size must be positive");

        trk.register_access_factory([](auto &tracker) {
            auto *self = LIBTCSPC_OBJECT_FROM_TRACKER(acquire_full_buckets,
                                                      trk, tracker);
            return acquire_access([self] { self->halt(); });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "acquire_full_buckets");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return merge_processor_graphs(
            live_downstream.introspect_graph().push_entry_point(this),
            batch_downstream.introspect_graph().push_entry_point(this));
    }

    void flush() {
        bucket<T> b;
        std::size_t filled = 0;
        {
            std::unique_lock lock(halt_mutex);
            while (not halted) {
                lock.unlock();
                auto const start_time = std::chrono::steady_clock::now();
                if (b.empty()) {
                    b = bsource->bucket_of_size(bsize);
                    filled = 0;
                }
                auto const unfilled = span(b).subspan(filled);
                std::optional<std::size_t> const read = reader(unfilled);
                if (not read)
                    return flush_downstreams(std::move(b), filled);
                if constexpr (not std::is_same_v<LiveDownstream, null_sink>)
                    emit_live(b, filled, *read);
                filled += *read;
                if (filled == bsize) {
                    emit_batch(std::move(b));
                    b = {};
                }
                lock.lock();
                if (filled < bsize) {
                    halt_cv.wait_until(lock, start_time + slow_acq_sleep,
                                       [&] { return halted; });
                }
            }
        }
        throw acquisition_halted();
    }
};

} // namespace internal

/**
 * \brief Create a processor that acquires data into buckets.
 *
 * \ingroup processors-acquisition
 *
 * This processor is used to integrate a pull-style device API (i.e., one in
 * which we make a function call into the driver to fill our buffer with
 * acquired data) as a data source for libtcspc. Data is acquired by the
 * provided reader (see \ref acquisition-readers).
 *
 * Every read from the reader uses an empty bucket; if any elements were read,
 * the bucket is passed downstream. In general, therefore, the buckets are
 * partially filled.
 *
 * \tparam T the element type of the acquired data (usually a byte or integer
 * type)
 *
 * \tparam Reader type of reader (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param reader reader
 *
 * \param buffer_provider bucket source providing event buffers
 *
 * \param batch_size number of elements (`T`) to collect in each bucket
 *
 * \param tracker access tracker for later access
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Flush: read from the reader until the end of stream, or until there is an
 *   error, or until halted, and emit (rvalue) `tcspc::bucket<T>` on each read.
 *   If end of stream is indicated by the reader, flush the downstream.
 */
template <typename T, typename Reader, typename Downstream>
auto acquire(Reader &&reader,
             std::shared_ptr<bucket_source<T>> buffer_provider,
             arg::batch_size<std::size_t> batch_size,
             access_tracker<acquire_access> tracker, Downstream &&downstream) {
    return internal::acquire<T, Reader, Downstream>(
        std::forward<Reader>(reader), std::move(buffer_provider), batch_size,
        std::move(tracker), std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that acquires data into buckets, ensuring that
 * each bucket is filled to a fixed size but also providing views of partial
 * buckets in real time.
 *
 * \ingroup processors-acquisition
 *
 * This processor is used to integrate a pull-style device API (i.e., one in
 * which we make a function call into the driver to fill our buffer with
 * acquired data) as a data source for libtcspc. Data is acquired by the
 * provided reader (see \ref acquisition-readers).
 *
 * The processor attaches two downstream processors. The \p live_downstream
 * receives newly acquired data as soon as it is available, but in the form of
 * a const view bucket. This is typically used for live processing and display.
 *
 * The \p batch_downstream recieves the same data, but only as each batch fills
 * up to the given \p batch_size (except for the last batch, which may be
 * smaller). This is typically used for saving the raw data to disk.
 *
 * The two streams share the underlying bucket storage.
 *
 * \tparam T the element type of the acquired data (usually a byte or  integer
 * type)
 *
 * \tparam Reader type of reader (usually deduced)
 *
 * \tparam LiveDownstream type of the "live" downstream processor (usually
 * deduced)
 *
 * \tparam BatchDownstream type of the "batch" downstream processor (usually
 * deduced)
 *
 * \param reader reader
 *
 * \param buffer_provider bucket source providing event buffers (must support
 * shared views unless \p LiveDownstream is `tcspc::null_sink`)
 *
 * \param batch_size number of elements (`T`) to collect in each bucket
 *
 * \param tracker access tracker for later access
 *
 * \param live_downstream downstream processor receiving read-only views of
 * data per each read
 *
 * \param batch_downstream downstream processor receiving full buckets
 *
 * \return processor
 *
 * \par Events handled
 * - Flush: read from the reader until the end of stream, or until there is an
 *   error, or until halted, and emit (const) `tcspc::bucket<T const>` to
 *   `live_downstream` on each read; emit (rvalue) `tcspc::bucket<T>` when
 *   `batch_size` elements have been collected, to `batch_downstream`. If end
 *   of stream is indicated by the reader, flush the downstream.
 */
template <typename T, typename Reader, typename LiveDownstream,
          typename BatchDownstream>
auto acquire_full_buckets(Reader &&reader,
                          std::shared_ptr<bucket_source<T>> buffer_provider,
                          arg::batch_size<std::size_t> batch_size,
                          access_tracker<acquire_access> tracker,
                          LiveDownstream &&live_downstream,
                          BatchDownstream &&batch_downstream) {
    return internal::acquire_full_buckets<T, Reader, LiveDownstream,
                                          BatchDownstream>(
        std::forward<Reader>(reader), std::move(buffer_provider), batch_size,
        std::move(tracker), std::forward<LiveDownstream>(live_downstream),
        std::forward<BatchDownstream>(batch_downstream));
}

/**
 * \brief Acquisition reader that reads an empty stream.
 *
 * \ingroup acquisition-readers
 */
template <typename T> struct null_reader {
    /** \brief Implements the acquisition reader requirement. */
    auto operator()([[maybe_unused]] span<T> buffer)
        -> std::optional<std::size_t> {
        return std::nullopt;
    }
};

/**
 * \brief Acquisition reader that waits indefinitely without producing data.
 *
 * \ingroup acquisition-readers
 */
template <typename T> struct stuck_reader {
    /** \brief Implements the acquisition reader requirement. */
    auto operator()([[maybe_unused]] span<T> buffer)
        -> std::optional<std::size_t> {
        return 0;
    }
};

} // namespace tcspc
