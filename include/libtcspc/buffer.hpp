/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "context.hpp"
#include "errors.hpp"
#include "introspect.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// Avoid std::hardware_destructive_interference_size, because it suffers from
// ABI compatibility requirements and therefore may not have the best value
// (for example, it seems to be 256 on Linux/aarch64). Instead, just default to
// 64 (correct for most (all?) x86_64 and many ARM processors) except in known
// cases where a larger value is appropriate.
#if defined(__APPLE__) && defined(__arm64__)
inline constexpr std::size_t destructive_interference_size = 128;
#else
inline constexpr std::size_t destructive_interference_size = 64;
#endif

} // namespace internal

/**
 * \brief Access for `tcspc::buffer()` and `tcspc::real_time_buffer()`
 * processors.
 *
 * \ingroup context-access
 */
class buffer_access {
    std::function<void()> halt_fn;
    std::function<void()> pump_fn;

  public:
    /** \private */
    template <typename HaltFunc, typename PumpFunc>
    explicit buffer_access(HaltFunc halt_func, PumpFunc pump_func)
        : halt_fn(halt_func), pump_fn(pump_func) {}

    /**
     * \brief Halt pumping of the buffer.
     *
     * The call to `pump()` will return (if it hasn't yet) without flushing the
     * downstream.
     *
     * This function must be called when:
     *
     * - Processing is being canceled at the source before flushing the
     *   processors upstream of the buffer.
     * - An error (an exception other than `tcspc::end_of_processing`) was
     *   received at the source while sending an event to, or flushing, the
     *   processors upstream of the buffer.
     *
     * These are two cases in which the buffer would otherwise have no
     * knowledge that the processing ended.
     *
     * It is also safe to call this function after `tcspc::end_of_processing`
     * was received at the source, or after the source successfully flushed the
     * processors upstream of the buffer. In these cases, the call has no
     * effect.
     */
    void halt() noexcept { halt_fn(); } // NOLINT(bugprone-exception-escape)

    /**
     * \brief Pump buffered events downstream.
     *
     * This function should be called on a thread other than the one on which
     * upstream events are sent to the buffer. Events will be emitted to the
     * buffer's downstream on the calling thread.
     *
     * Depending on how the processing ends, this function exits in different
     * ways:
     *
     * - If a flush is propagated from upstream of the buffer to its
     *   downstream, this function returns.
     * - If an exception (either `tcspc::end_of_processing` or an error) is
     *   thrown downstream of the buffer, this function throws that exception.
     * - If `halt()` is called before the buffer receives a flush from its
     *   upstream, this function throws `tcspc::source_halted`.
     *
     * Applications should generally report errors for exceptions other than
     * `tcspc::end_of_processing` and `tcspc::source_halted`. Note that such
     * exceptions are _not_ propagated to upstream processors (because there
     * may not be the opportunity to do so if the upstream never calls the
     * buffer again).
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
    access_tracker<buffer_access> trk;

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
                    downstream.handle(std::move(emit_queue.front()));
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
    template <typename Rep, typename Period>
    explicit buffer(arg::threshold<std::size_t> threshold,
                    std::chrono::duration<Rep, Period> latency_limit,
                    access_tracker<buffer_access> &&tracker,
                    Downstream downstream)
        : threshold(threshold.value >= 0 ? threshold.value : 1),
          max_latency(
              std::chrono::duration_cast<clock_type::duration>(latency_limit)),
          downstream(std::move(downstream)), trk(std::move(tracker)) {
        // Limit to avoid integer overflow.
        if (max_latency > std::chrono::hours(24)) {
            throw std::invalid_argument(
                "buffer latency limit must not be greater than 24 h");
        }

        trk.register_access_factory([](auto &tracker) {
            auto *self = LIBTCSPC_OBJECT_FROM_TRACKER(buffer, trk, tracker);
            return buffer_access([self] { self->halt(); },
                                 [self] { self->pump(); });
        });
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    explicit buffer(arg::threshold<std::size_t> threshold,
                    access_tracker<buffer_access> &&tracker,
                    Downstream downstream)
        : buffer(threshold, std::chrono::hours(24), std::move(tracker),
                 std::move(downstream)) {}
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "buffer");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E,
              typename = std::enable_if_t<std::is_convertible_v<E, Event>>>
    void handle(E &&event) {
        bool should_notify{};
        {
            std::scoped_lock lock(mutex);
            if (downstream_threw)
                throw end_of_processing(
                    "ending upstream of buffer upon end of downstream processing");

            shared_queue.push(std::forward<E>(event));
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
                throw end_of_processing(
                    "ending upstream of buffer upon end of downstream processing");
            upstream_flushed = true;
        }
        has_data_condition.notify_one();
    }
};

} // namespace internal

/**
 * \brief Create a processor that buffers events and emits them on a different
 * thread.
 *
 * \ingroup processors-buffering
 *
 * The processor receives events of type \p Event from upstream like a normal
 * processor, but stores them in a buffer. By _pumping_ on a different thread,
 * the buffered events can be sent downstream on that thread.
 *
 * On the pumping thread, events are emitted downstream when the number of
 * buffered events reaches the \p threshold.
 *
 * The upstream thread (the thread sending events to this processor) must
 * _halt_ this processor when it will not send anything more. Note that halting
 * is required even if upstream processing terminated by an exception
 * (including during an explicit flush), because such an exception may have
 * been thrown upstream of the buffer without its knowledge.
 *
 * Pumping and halting is done through a `tcspc::buffer_access` object
 * retrieved from the `tcspc::context` from which \p tracker was
 * obtained.
 *
 * \see `tcspc::process_in_batches()`
 * \see `tcspc::real_time_buffer()`
 *
 * \tparam Event the event type
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param threshold number of events to buffer before start sending to
 * downstream
 *
 * \param tracker access tracker for later access
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: buffer and pass through on the pumping thread; throw
 *   `tcspc::end_of_processing` if pumping thread has exited (normally or with
 *   error)
 * - Flush: buffer and pass through on the pumping thread; throw
 *   `tcspc::end_of_processing` if pumping thread has exited (normally or with
 *   error)
 */
template <typename Event, typename Downstream>
auto buffer(arg::threshold<std::size_t> threshold,
            access_tracker<buffer_access> &&tracker, Downstream &&downstream) {
    return internal::buffer<Event, false, Downstream>(
        threshold, std::move(tracker), std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that buffers events and emits them on a different
 * thread, with limited latency.
 *
 * \ingroup processors-buffering
 *
 * The processor receives events of type \p Event from upstream like a normal
 * processor, but stores them in a buffer. By _pumping_ on a different thread,
 * the buffered events can be sent downstream on that thread.
 *
 * On the pumping thread, events are emitted downstream when either the number
 * of buffered events reaches the \p threshold or when the oldest event has
 * been buffered for a duration of at least \p latency_limit.
 *
 * The upstream thread (the thread sending events to this processor) must
 * _halt_ this processor when it will not send anything more. Note that halting
 * is required even if upstream processing terminated by an exception
 * (including during an explicit flush), because such an exception may have
 * been thrown upstream of the buffer without its knowledge.
 *
 * Pumping and halting is done through a `tcspc::buffer_access` object
 * retrieved from the `tcspc::context` from which \p tracker was
 * obtained.
 *
 * \see `tcspc::buffer()`
 *
 * \tparam Event the event type
 *
 * \tparam Rep tick count type of duration type of \p latency_limit (usually
 * deduced)
 *
 * \tparam Period period of duration type of \p latency_limit (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param threshold number of events to buffer before start sending to
 * downstream
 *
 * \param latency_limit the maximum time an event can remain in the buffer
 * before sending to downstream is started even if there are fewer events than
 * threshold. Must not exceed 24 hours.
 *
 * \param tracker access tracker for later access
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: buffer and pass through on pumping thread; throw
 *   `tcspc::end_of_processing` if pumping thread has exited (normally or with
 *   error)
 * - Flush: buffer and pass through on the pumping thread; throw
 *   `tcspc::end_of_processing` if pumping thread has exited (normally or with
 *   error)
 */
template <typename Event, typename Rep, typename Period, typename Downstream>
auto real_time_buffer(arg::threshold<std::size_t> threshold,
                      std::chrono::duration<Rep, Period> latency_limit,
                      access_tracker<buffer_access> &&tracker,
                      Downstream &&downstream) {
    return internal::buffer<Event, true, Downstream>(
        threshold, latency_limit, std::move(tracker),
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
