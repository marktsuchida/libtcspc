/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "processor_context.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>

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

/**
 * \brief Accessor for buffer processor.
 *
 * \ingroup processors-basic
 *
 * \see buffer
 * \see real_time_buffer
 */
class buffer_accessor {
    std::function<void()> halt_fn;
    std::function<void()> pump_fn;

  public:
    /** \brief Constructor; not for client use. */
    template <typename HaltFunc, typename PumpFunc>
    explicit buffer_accessor(HaltFunc halt_func, PumpFunc pump_func)
        : halt_fn(halt_func), pump_fn(pump_func) {}

    /**
     * \brief Halt pumping of the buffer.
     *
     * The call to pump() will return without flushing the downstream.
     *
     * This function must always be called when the upstream processor will no
     * longer send events (or a flush) to the buffer. This includes when
     * upstream processing terminated by an exception (including during an
     * explicit flush), because such an exception may have been thrown upstream
     * of the buffer without its knowledge.
     */
    void halt() noexcept { halt_fn(); } // NOLINT(bugprone-exception-escape)

    /**
     * \brief Pump buffered events downstream.
     *
     * This function should be called on a thread other than the one on which
     * upstream events are sent. Events will be emitted downstream on the
     * calling thread.
     *
     * This function exits normally when a flush has been propagated from
     * upstream to downstream without an exception being thrown. If an
     * exception is thrown by a downstream processor (including \ref
     * end_processing), it is propagated out of this function. If halt() is
     * called when events are still being pumped, this function throws \ref
     * source_halted.
     *
     * Applications should generally report errors for exceptions other than
     * \ref end_processing and \ref source_halted. Note that such exceptions
     * are not propagated to upstream processors (this is because there may not
     * be the opportunity to do so if the upstream never calls the buffer
     * again).
     */
    void pump() { pump_fn(); }
};

namespace internal {

template <typename Event, bool LatencyLimited, typename Downstream>
class buffer {
    using clock_type = std::chrono::steady_clock;
    using queue_type = vector_queue<Event>;

    std::size_t threshold;
    clock_type::duration max_latency;

    std::mutex mutex;
    std::condition_variable has_data_condition;
    queue_type shared_queue;
    clock_type::time_point oldest_enqueued_time;
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

    // Cold data after downstream.
    bool pumped = false;
    processor_tracker<buffer_accessor> trk;

    void halt() noexcept {
        {
            std::scoped_lock lock(mutex);
            upstream_halted = true;
        }
        has_data_condition.notify_one();
    }

    void pump() {
        try {
            std::unique_lock lock(mutex);
            if (pumped) {
                throw std::logic_error(
                    "buffer may not be pumped a second time");
            }
            pumped = true;

            for (;;) {
                if constexpr (LatencyLimited) {
                    has_data_condition.wait(lock, [&] {
                        return not shared_queue.empty() || upstream_flushed ||
                               upstream_halted;
                    });
                    // Won't overflow due to 24 h limit on max_latency:
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

                if (not upstream_flushed && upstream_halted)
                    throw source_halted();
                if (shared_queue.empty() && upstream_flushed) {
                    lock.unlock();
                    return downstream.flush();
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

  public:
    template <typename Duration>
    explicit buffer(std::size_t threshold, Duration latency_limit,
                    processor_tracker<buffer_accessor> &&tracker,
                    Downstream downstream)
        : threshold(threshold >= 0 ? threshold : 1),
          max_latency(
              std::chrono::duration_cast<clock_type::duration>(latency_limit)),
          downstream(std::move(downstream)), trk(std::move(tracker)) {
        // Limit to avoid integer overflow.
        if (max_latency > std::chrono::hours(24)) {
            throw std::logic_error(
                "buffer latency limit must not be greater than 24 h");
        }

        trk.register_accessor_factory([](auto &tracker) {
            auto *self = LIBTCSPC_PROCESSOR_FROM_TRACKER(buffer, trk, tracker);
            return buffer_accessor([self] { self->halt(); },
                                   [self] { self->pump(); });
        });
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    explicit buffer(std::size_t threshold,
                    processor_tracker<buffer_accessor> &&tracker,
                    Downstream downstream)
        : buffer(threshold, std::chrono::hours(24), std::move(tracker),
                 std::move(downstream)) {}
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)

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
};

} // namespace internal

/**
 * \brief Create a pseudo-processor that buffers events and emits them on a
 * different thread.
 *
 * \ingroup processors-basic
 *
 * This receives events of type \c Event from upstream like a normal processor,
 * but stores them in a buffer. By pumping on a different thread (see
 * buffer_accessor::pump()), the buffered events can be sent downstream on that
 * thread.
 *
 * Events are emitted downstream when the number of buffered events reaches the
 * \p threshold.
 *
 * The thread sending events to the buffer must notify the buffer via
 * buffer_accessor::halt() when it will not send anything more. Note that this
 * call is required even if upstream processing terminated by an exception
 * (including during an explicit flush), because such an exception may have
 * been thrown upstream of the buffer without its knowledge.
 *
 * \tparam Event the event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param threshold number of events to buffer before start sending to
 * downstream
 *
 * \param tracker processor tracker for later access
 *
 * \param downstream downstream processor
 */
template <typename Event, typename Downstream>
auto buffer(std::size_t threshold,
            processor_tracker<buffer_accessor> &&tracker,
            Downstream &&downstream) {
    return internal::buffer<Event, false, Downstream>(
        threshold, std::move(tracker), std::forward<Downstream>(downstream));
}

/**
 * \brief Create a pseudo-processor that buffers events and emits them on a
 * different thread, with limited latency.
 *
 * \ingroup processors-basic
 *
 * This receives events of type \c Event from upstream like a normal processor,
 * but stores them in a buffer. By pumping on a different thread (see
 * buffer_accessor::pump()), the buffered events can be sent downstream on that
 * thread.
 *
 * Events are emitted downstream when either the number of buffered events
 * reaches the \p threshold or when the oldest event has been buffered for at
 * least \p latency_limit.
 *
 * The thread sending events to the buffer must notify the buffer via
 * buffer_accessor::halt() when it will not send anything more. Note that this
 * call is required even if upstream processing terminated by an exception
 * (including during an explicit flush), because such an exception may have
 * been thrown upstream of the buffer without its knowledge.
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
 * even if there are fewer events than threshold. Must not exceed 24 hours.
 *
 * \param tracker processor tracker for later access
 *
 * \param downstream downstream processor
 */
template <typename Event, typename Duration, typename Downstream>
auto real_time_buffer(std::size_t threshold, Duration latency_limit,
                      processor_tracker<buffer_accessor> &&tracker,
                      Downstream &&downstream) {
    return internal::buffer<Event, true, Downstream>(
        threshold, latency_limit, std::move(tracker),
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
