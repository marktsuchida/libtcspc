/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "vector_queue.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
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
 * additional instances on demand.
 *
 * Note that behavior is undefined unless all checked out objects are released
 * before the pool is destroyed.
 *
 * \tparam T the object type
 */
template <typename T> class object_pool {
    std::mutex mutex;
    std::vector<std::unique_ptr<T>> buffers;

  public:
    /**
     * \brief Construct with initial count.
     *
     * \param initial_count number of \c T instances to pre-allocate
     */
    explicit object_pool(std::size_t initial_count = 0) {
        buffers.reserve(initial_count);
        std::generate_n(std::back_inserter(buffers), initial_count,
                        [] { return std::make_unique<T>(); });
    }

    /**
     * \brief Obtain an object for use.
     *
     * The returned shared pointer has a deleter that will automatically return
     * (check in) the object back to this pool.
     *
     * Note that all checked out objects must be released (by allowing all
     * shared pointers to be destroyed) before the pool is destroyed.
     *
     * \return shared pointer to the checked out object
     */
    auto check_out() -> std::shared_ptr<T> {
        std::unique_ptr<T> uptr;

        {
            std::scoped_lock hold(mutex);
            if (!buffers.empty()) {
                uptr = std::move(buffers.back());
                buffers.pop_back();
            }
        }

        if (!uptr)
            uptr = std::make_unique<T>();

        return {uptr.release(), [this](auto ptr) {
                    if (ptr != nullptr) {
                        std::scoped_lock hold(mutex);
                        buffers.emplace_back(std::unique_ptr<T>(ptr));
                    }
                }};
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

template <typename Event, typename Downstream> class buffer_event {
    std::mutex mutex;
    std::condition_variable has_item_condition; // item = event or end

    using queue_type = vector_queue<Event>;
    queue_type shared_queue;
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
    explicit buffer_event(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void handle_event(Event const &event) noexcept {
        bool was_empty{};
        {
            std::scoped_lock lock(mutex);
            if (stream_ended)
                return;

            was_empty = shared_queue.empty();
            try {
                shared_queue.push(event);
            } catch (std::exception const &) {
                stream_ended = true;
                queued_error = std::current_exception();
            }
        }
        if (was_empty)
            has_item_condition.notify_one();
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        {
            std::scoped_lock lock(mutex);
            if (stream_ended)
                return;

            stream_ended = true;
            queued_error = error;
        }
        has_item_condition.notify_one();
    }

    void pump_downstream() noexcept {
        std::unique_lock lock(mutex);

        for (;;) {
            has_item_condition.wait(
                lock, [&] { return !shared_queue.empty() || stream_ended; });

            if (shared_queue.empty()) { // Implying stream ended
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
 * but stores them in a buffer. By calling <tt>void pump_downstream()
 * noexcept</tt> on a different thread, the buffered events can be sent
 * downstream on that thread. The \c pump_downstream function blocks until the
 * upstream has signaled the end of stream and all events have been emitted
 * downstream.
 *
 * Usually \c Event should be EventArray in order to reduce overhead.
 *
 * \tparam Event the event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return buffer-events pseudo-processor
 */
template <typename Event, typename Downstream>
auto buffer_event(Downstream &&downstream) {
    return internal::buffer_event<Event, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
