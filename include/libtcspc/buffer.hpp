/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "vector_queue.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <type_traits>
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
 * \brief Memory pool holding objects for reuse.
 *
 * \ingroup misc
 *
 * In other words, a free list of \c T instances that automatically allocates
 * additional instances on demand (up to a count limit, upon which the request
 * blocks).
 *
 * Instances must be handled via \c std::shared_ptr and do not allow move or
 * copy.
 *
 * Note that all objects created by the pool remain allocated until the pool is
 * destroyed, which only happens once all shared pointers to pool objects have
 * been destroyed or reset.
 *
 * \tparam T the object type (must be default-constructible)
 */
template <typename T>
class object_pool : public std::enable_shared_from_this<object_pool<T>> {
    std::mutex mutex;
    std::condition_variable not_empty_condition;
    std::vector<std::unique_ptr<T>> objects;
    std::size_t max_objects;
    std::size_t object_count = 0;

    auto make_checked_out_ptr(std::unique_ptr<T> uptr) -> std::shared_ptr<T> {
        return {uptr.release(), [self = this->shared_from_this()](auto ptr) {
                    if (ptr != nullptr) {
                        {
                            std::scoped_lock lock(self->mutex);
                            self->objects.emplace_back(ptr);
                        }
                        self->not_empty_condition.notify_one();
                    }
                }};
    }

  public:
    /**
     * \brief Construct an object pool; to be used with \c std::make_shared.
     *
     * Make sure to manage the instance with \c std::shared_ptr.
     *
     * \param initial_count number of \c T instances to pre-allocate (must not
     * be greater than max_count)
     *
     * \param max_count maximum number of \c T instances to have in circulation
     * at any time (must be positive)
     */
    explicit object_pool(
        std::size_t initial_count = 0,
        std::size_t max_count = std::numeric_limits<std::size_t>::max())
        : max_objects(max_count) {
        assert(initial_count <= max_count);
        assert(max_count > 0);
        objects.reserve(initial_count);
        std::generate_n(std::back_inserter(objects), initial_count,
                        [] { return std::make_unique<T>(); });
        object_count = initial_count;
    }

    /**
     * \brief Obtain an object for use, if available without blocking.
     *
     * If there are no available objects and the maximum allowed number are
     * already in circulation, this function will return immediately an empty
     * shared pointer.
     *
     * The returned shared pointer has a deleter that will automatically return
     * (check in) the object back to this pool.
     *
     * Note that all checked out objects must be released (by allowing all
     * shared pointers to be destroyed) before the pool is destroyed.
     *
     * \return shared pointer to the checked out object, or empty if none are
     * available immediately
     *
     * \throws std::bad_alloc if allocation failed
     */
    auto maybe_check_out() -> std::shared_ptr<T> {
        std::unique_ptr<T> uptr;
        bool should_allocate = false;

        {
            std::unique_lock lock(mutex);
            if (objects.empty() && object_count < max_objects) {
                should_allocate = true;
                ++object_count;
            } else if (objects.empty()) {
                return {};
            }
            uptr = std::move(objects.back());
            objects.pop_back();
        }

        if (should_allocate)
            uptr = std::make_unique<T>();

        return make_checked_out_ptr(std::move(uptr));
    }

    /**
     * \brief Obtain an object for use, blocking if necessary.
     *
     * If there are no available objects and the maximum allowed number are
     * already in circulation, this function will block until an object is
     * available.
     *
     * The returned shared pointer has a deleter that will automatically return
     * (check in) the object back to this pool.
     *
     * Note that all checked out objects must be released (by allowing all
     * shared pointers to be destroyed) before the pool is destroyed.
     *
     * \return shared pointer to the checked out object
     *
     * \throws std::bad_alloc if allocation failed
     */
    auto check_out() -> std::shared_ptr<T> {
        std::unique_ptr<T> uptr;
        bool should_allocate = false;

        {
            std::unique_lock lock(mutex);
            if (objects.empty() && object_count < max_objects) {
                should_allocate = true;
                ++object_count;
            } else {
                not_empty_condition.wait(lock,
                                         [&] { return not objects.empty(); });
                uptr = std::move(objects.back());
                objects.pop_back();
            }
        }

        if (should_allocate)
            uptr = std::make_unique<T>();

        return make_checked_out_ptr(std::move(uptr));
    }
};

namespace internal {

template <typename Pointer, typename Downstream> class dereference_pointer {
    Downstream downstream;

  public:
    using event_type = decltype(*std::declval<Pointer>);

    explicit dereference_pointer(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void handle_event(Pointer const &event_ptr) noexcept {
        downstream.handle_event(*event_ptr);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor dereferencing a pointers to events.
 *
 * \ingroup processors-basic
 *
 * This can be used, for example, to convert \c shared_pointer<Event> to \c
 * Event for some event type \c Event.
 *
 * \tparam Pointer the event pointer type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return dereference-pointer processor
 */
template <typename Pointer, typename Downstream>
auto dereference_pointer(Downstream &&downstream) {
    return internal::dereference_pointer<Pointer, Downstream>(
        std::forward<Downstream>(downstream));
}

namespace internal {

template <typename EventContainer, typename Event, typename Downstream>
class unbatch {
    Downstream downstream;

  public:
    explicit unbatch(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void handle_event(EventContainer const &events) noexcept {
        for (auto const &event : events)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor transforming batches of events to individual
 * events.
 *
 * \ingroup processors-basic
 *
 * \tparam EventContainer event container type
 *
 * \tparam Event the event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return unbatch processor
 */
template <typename EventContainer, typename Event, typename Downstream>
auto unbatch(Downstream &&downstream) {
    return internal::unbatch<EventContainer, Event, Downstream>(
        std::forward<Downstream>(downstream));
}

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
    bool stream_ended = false;
    std::exception_ptr queued_error;

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
                    Downstream &&downstream)
        : threshold(threshold),
          max_latency(std::chrono::duration_cast<decltype(max_latency)>(
              latency_limit)),
          downstream(std::move(downstream)) {}

    explicit buffer(std::size_t threshold, Downstream &&downstream)
        : threshold(threshold), downstream(std::move(downstream)) {}

    void handle_event(Event const &event) noexcept {
        bool should_notify{};
        {
            std::scoped_lock lock(mutex);
            if (stream_ended)
                return;

            try {
                shared_queue.push(event);
                should_notify = shared_queue.size() == threshold;
                if constexpr (LatencyLimited) {
                    if (shared_queue.size() == 1) {
                        oldest_enqueued_time = clock_type::now();
                        should_notify = true; // Wake up once to set deadline.
                    }
                }
            } catch (std::exception const &) {
                stream_ended = true;
                queued_error = std::current_exception();
                should_notify = true;
            }
        }
        if (should_notify)
            has_data_condition.notify_one();
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        {
            std::scoped_lock lock(mutex);
            if (stream_ended)
                return;

            stream_ended = true;
            queued_error = error;
        }
        has_data_condition.notify_one();
    }

    void pump_events() noexcept {
        std::unique_lock lock(mutex);

        for (;;) {
            if constexpr (LatencyLimited) {
                has_data_condition.wait(lock, [&] {
                    return not shared_queue.empty() || stream_ended;
                });
                auto const deadline = oldest_enqueued_time + max_latency;
                has_data_condition.wait_until(lock, deadline, [&] {
                    return shared_queue.size() >= threshold || stream_ended;
                });
            } else {
                has_data_condition.wait(lock, [&] {
                    return shared_queue.size() >= threshold || stream_ended;
                });
            }

            if (stream_ended && shared_queue.empty()) {
                std::exception_ptr error;
                std::swap(error, queued_error);
                lock.unlock();
                downstream.handle_end(error);
                return;
            }

            emit_queue.swap(shared_queue);

            lock.unlock();

            while (!emit_queue.empty()) {
                downstream.handle_event(emit_queue.front());
                emit_queue.pop();
            }

            lock.lock();
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
 * but stores them in a buffer. By calling <tt>void pump_events() noexcept</tt>
 * on a different thread, the buffered events can be sent downstream on that
 * thread. The \c pump_events function blocks until the upstream has signaled
 * the end of stream and all events have been emitted downstream.
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
 * \param downstream downstream processor (moved out)
 *
 * \return buffer pseudo-processor having \c pump_events member function
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
 * but stores them in a buffer. By calling <tt>void pump_events() noexcept</tt>
 * on a different thread, the buffered events can be sent downstream on that
 * thread. The \c pump_events function blocks until the upstream has signaled
 * the end of stream and all events have been emitted downstream.
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
 * \param downstream downstream processor (moved out)
 *
 * \return buffer pseudo-processor having \c pump_events member function
 */
template <typename Event, typename Duration, typename Downstream>
auto real_time_buffer(std::size_t threshold, Duration latency_limit,
                      Downstream &&downstream) {
    return internal::buffer<Event, true, Downstream>(
        threshold, latency_limit, std::forward<Downstream>(downstream));
}

} // namespace tcspc
