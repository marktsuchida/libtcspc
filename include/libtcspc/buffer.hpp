/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

// Avoid std::hardware_destructive_interference_size, because it suffers from
// ABI compatibility requirements and therefore may not have the best value
// (for example, it seems to be 256 on Linux/aarch64). Instead, just default to
// 64 (correct for most (all?) x86_64 and many ARM processors) except in known
// cases where a larger value is appropriate.
#if defined(__APPLE__) && defined(__arm64__)
constexpr std::size_t destructive_interference_size = 128;
#else
constexpr std::size_t destructive_interference_size = 64;
#endif

} // namespace internal

/**
 * \brief Exception type thrown when buffer source was discontinued without
 * reaching the point of flushing.
 */
class source_halted final : std::exception {
  public:
    /** \brief std::exception interface. */
    [[nodiscard]] auto what() const noexcept -> char const * override {
        return "source halted without flushing";
    }
};

namespace internal {

template <typename Event, bool LatencyLimited, typename Downstream>
class buffer {
    using clock_type = std::chrono::steady_clock;
    using queue_type = vector_queue<Event>;

    std::size_t threshold;
    std::chrono::time_point<clock_type>::duration max_latency =
        std::chrono::time_point<clock_type>::duration::max();

    std::mutex mutex;
    std::condition_variable has_data_condition;
    queue_type shared_queue;
    std::chrono::time_point<clock_type> oldest_enqueued_time;
    bool upstream_flushed = false;
    bool upstream_halted = false;
    bool downstream_threw = false;

    // To reduce lock contention on the shared_queue, we use a second queue
    // that is accessed only by the emitting thread and is not protected by the
    // mutex. Events in the shared_queue are transferred in bulk to the
    // emit_queue while the mutex is held.
    // This means that the mutex does not need to be acquired between every
    // event emitted, so the producer will be less likely to block when the
    // data rate is momentarily high, and the consumer will be less likely to
    // block while catching up on buffered events.
    // Furthermore, we ensure that the emit_queue and downstream do not share a
    // CPU cache line with the shared_queue, to prevent false sharing.
    alignas(destructive_interference_size) queue_type emit_queue;

    Downstream downstream;

  public:
    template <typename Duration>
    explicit buffer(std::size_t threshold, Duration latency_limit,
                    Downstream downstream)
        : threshold(threshold),
          max_latency(std::chrono::duration_cast<decltype(max_latency)>(
              latency_limit)),
          downstream(std::move(downstream)) {}

    explicit buffer(std::size_t threshold, Downstream downstream)
        : threshold(threshold), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "buffer");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Event const &event) {
        bool should_notify{};
        {
            std::scoped_lock lock(mutex);
            if (downstream_threw)
                throw end_processing(
                    "ending upstream of buffer upon end of downstream processing");

            shared_queue.push(event);
            should_notify = shared_queue.size() == threshold;
            if constexpr (LatencyLimited) {
                if (shared_queue.size() == 1) {
                    oldest_enqueued_time = clock_type::now();
                    should_notify = true; // Wake up once to set deadline.
                }
            }
        }
        if (should_notify)
            has_data_condition.notify_one();
    }

    void flush() {
        {
            std::scoped_lock lock(mutex);
            if (downstream_threw)
                throw end_processing(
                    "ending upstream of buffer upon end of downstream processing");
            upstream_flushed = true;
        }
        has_data_condition.notify_one();
    }

    void halt() noexcept {
        {
            std::scoped_lock lock(mutex);
            upstream_halted = true;
        }
        has_data_condition.notify_one();
    }

    void pump() {
        if (upstream_flushed || upstream_halted || downstream_threw)
            throw std::logic_error("buffer may not be pumped a second time");

        try {
            std::unique_lock lock(mutex);
            for (;;) {
                if constexpr (LatencyLimited) {
                    has_data_condition.wait(lock, [&] {
                        return not shared_queue.empty() || upstream_flushed ||
                               upstream_halted;
                    });
                    auto const deadline = oldest_enqueued_time + max_latency;
                    has_data_condition.wait_until(lock, deadline, [&] {
                        return shared_queue.size() >= threshold ||
                               upstream_flushed || upstream_halted;
                    });
                } else {
                    has_data_condition.wait(lock, [&] {
                        return shared_queue.size() >= threshold ||
                               upstream_flushed || upstream_halted;
                    });
                }

                if (shared_queue.empty()) {
                    if (upstream_flushed) {
                        lock.unlock();
                        downstream.flush();
                        return;
                    }
                    if (upstream_halted) // Ended without flushing.
                        throw source_halted();
                }

                emit_queue.swap(shared_queue);

                lock.unlock();

                while (!emit_queue.empty()) {
                    downstream.handle(emit_queue.front());
                    emit_queue.pop();
                }

                lock.lock();
            }
        } catch (source_halted const &) {
            throw;
        } catch (...) {
            std::scoped_lock lock(mutex);
            downstream_threw = true;
            throw;
        }
    }
};

} // namespace internal

/**
 * \brief Create a pseudo-processor that buffers events.
 *
 * \ingroup processors-basic
 *
 * This receives events of type \c Event from upstream like a normal processor,
 * but stores them in a buffer. By calling `void pump()` on a different thread,
 * the buffered events can be sent downstream on that thread. The \c pump
 * function blocks until the upstream has signaled the end of stream and all
 * events have been emitted downstream.
 *
 * Usually \c Event should be EventArray in order to reduce overhead.
 *
 * \tparam Event the event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param threshold number of events to buffer before start sending to
 * downstream
 *
 * \param downstream downstream processor
 *
 * \return buffer pseudo-processor having \c pump member function
 *
 * \todo Currently there is no reasonable way to pump the buffer once it has
 * been placed within a processing graph. This needs to be fixed by introducing
 * a processor tracker.
 */
template <typename Event, typename Downstream>
auto buffer(std::size_t threshold, Downstream &&downstream) {
    return internal::buffer<Event, false, Downstream>(
        threshold, std::forward<Downstream>(downstream));
}

/**
 * \brief Create a pseudo-processor that buffers events with limited latency.
 *
 * \ingroup processors-basic
 *
 * This receives events of type \c Event from upstream like a normal processor,
 * but stores them in a buffer. By calling `void pump()` on a different thread,
 * the buffered events can be sent downstream on that thread. The \c pump
 * function blocks until the upstream has signaled the end of stream and all
 * events have been emitted downstream.
 *
 * The thread sending events to the buffer must call `void halt() noexcept`
 * when it will not send anything more. Note that this call is required even if
 * processing terminated by an exception (including during an explicit flush),
 * because such an exception may have been thrown upstream of the buffer
 * without its knowledge. Without the call to \c halt(), the downstream call to
 * \c pump() may block indefinitely.
 *
 * Usually \c Event should be EventArray in order to reduce overhead.
 *
 * \tparam Event the event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param threshold number of events to buffer before start sending to
 * downstream
 *
 * \param latency_limit a \c std::chrono::duration specifying the maximum time
 * an event can remain in the buffer before sending to downstream is started
 * even if there are fewer events than threshold
 *
 * \param downstream downstream processor
 *
 * \return buffer pseudo-processor having \c pump member function
 *
 * \todo Currently there is no reasonable way to pump the buffer once it has
 * been placed within a processing graph. This needs to be fixed by introducing
 * a processor tracker.
 */
template <typename Event, typename Duration, typename Downstream>
auto real_time_buffer(std::size_t threshold, Duration latency_limit,
                      Downstream &&downstream) {
    return internal::buffer<Event, true, Downstream>(
        threshold, latency_limit, std::forward<Downstream>(downstream));
}

namespace internal {

template <typename Event, typename Downstream> class single_threaded_buffer {
    std::size_t threshold;
    std::vector<Event> buf;

    Downstream downstream;

    LIBTCSPC_NOINLINE void drain() {
        for (auto &event : buf)
            downstream.handle(event);
        buf.clear();
    }

  public:
    explicit single_threaded_buffer(std::size_t threshold,
                                    Downstream downstream)
        : threshold(threshold), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "single_threaded_buffer");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Event const &event) {
        buf.push_back(event);
        if (buf.size() >= threshold)
            drain();
    }

    void flush() {
        drain();
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that buffers events and passes them downstream
 * when a threshold capacity is reached.
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
 * Events are buffered until the threshold is reached, without regard to
 * timing, so this type of buffer is usually not appropriate for live
 * processing.
 *
 * \see buffer
 *
 * \tparam Event the event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param threshold number of events to buffer before passing them downstream
 *
 * \param downstream downstream processor
 *
 * \return single-threaded-buffer processor
 */
template <typename Event, typename Downstream>
auto single_threaded_buffer(std::size_t threshold, Downstream &&downstream) {
    return internal::single_threaded_buffer<Event, Downstream>(
        threshold, std::forward<Downstream>(downstream));
}

} // namespace tcspc
