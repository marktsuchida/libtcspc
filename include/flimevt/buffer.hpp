/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

namespace flimevt {

/**
 * \brief Memory pool holding objects for reuse.
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
    std::shared_ptr<T> check_out() {
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

/**
 * \brief Processor dereferencing a pointers to events.
 *
 * This can be used, for example, to convert \c shared_pointer<E> to \c E for
 * some even type \c E.
 *
 * \tparam P the event pointer type
 * \tparam D downstream processor type
 */
template <typename P, typename D> class dereference_pointer {
    D downstream;

  public:
    /**
     * \brief The type of the dereferenced event.
     */
    using event_type = decltype(*std::declval<P>);

    /**
     * \brief Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit dereference_pointer(D &&downstream)
        : downstream(std::move(downstream)) {}

    /** \brief Processor interface */
    void handle_event(P const &event_ptr) noexcept {
        downstream.handle_event(*event_ptr);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

/**
 * \brief Processor transforming batches of events to individual events.
 *
 * \tparam V event container type
 * \tparam E the event type
 * \tparam D downstream processor type
 */
template <typename V, typename E, typename D> class unbatch {
    D downstream;

  public:
    /**
     * Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit unbatch(D &&downstream) : downstream(std::move(downstream)) {}

    /** \brief Processor interface */
    void handle_event(V const &events) noexcept {
        for (auto const &event : events)
            downstream.handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

/**
 * \brief A pseudo-processor that buffers events.
 *
 * This receives events of type \c E from upstream like a normal processor, but
 * stores them in a buffer. By calling pump_downstream() on a different thread,
 * the buffered events can be sent downstream on that thread.
 *
 * Usually \c E should be EventArray in order to reduce overhead.
 *
 * \tparam E the event type
 * \tparam D downstream processor type
 */
template <typename E, typename D> class buffer_event {
    std::mutex mutex;
    std::condition_variable has_item_condition; // item = event or end

    std::queue<E> shared_queue;
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
    // The emit_queue is always empty at resting, but we keep it in a data
    // member in order to reuse allocated memory.
    // Note: Swapping the queues is not theoretically necessary when the queue
    // is based on std::deque (which never invalidates iterators): the range of
    // items could safely be accessed with the mutex released given that no
    // other thread will remove elements. But std::queue only gives access to
    // front() and in any case we may want to use something more efficient that
    // deque in the future (which may reallocate), so let's keep it simple.
    std::queue<E> emit_queue; // Invariant: always empty

    D downstream;

  public:
    /**
     * \brief Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit buffer_event(D &&downstream)
        : downstream(std::move(downstream)) {}

    /** \brief Processor interface */
    void handle_event(E const &event) noexcept {
        {
            std::scoped_lock lock(mutex);
            if (stream_ended)
                return;

            try {
                shared_queue.push(event);
            } catch (std::exception const &) {
                stream_ended = true;
                queued_error = std::current_exception();
            }
        }
        has_item_condition.notify_one();
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        {
            std::scoped_lock lock(mutex);
            if (stream_ended)
                return;

            stream_ended = true;
            queued_error = error;
        }
        has_item_condition.notify_one();
    }

    /**
     * \brief Send buffered events downstream on the caller's thread.
     *
     * This function blocks until the upstream has singaled the end of stream
     * and all events have been emitted downstream.
     */
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

} // namespace flimevt
