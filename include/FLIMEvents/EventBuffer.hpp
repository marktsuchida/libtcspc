/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace flimevt {

/**
 * \brief Fixed-capacity reusable array to hold events for buffering.
 *
 * \tparam E the event type to store
 */
template <typename E> class EventArray {
    std::size_t const capacity;
    std::size_t size;
    std::unique_ptr<E[]> events;

  public:
    /**
     * \brief Construct with capacity.
     *
     * \param capacity the maximum number of \c E events held by the array
     */
    explicit EventArray(std::size_t capacity)
        : capacity(capacity), size(0), events(new E[capacity]) {}

    /**
     * \brief Return the capacity.
     */
    std::size_t GetCapacity() const noexcept { return capacity; }

    /**
     * \brief Return the number of \c E events contained in this array.
     */
    std::size_t GetSize() const noexcept { return size; }

    /**
     * \brief Set the number of \c E events contained in this array.
     *
     * The actual data is not altered; uninitialized garbage will result if the
     * size is expanded.
     */
    void SetSize(std::size_t size) noexcept { this->size = size; }

    /**
     * \brief Return a mutable pointer to the event array buffer.
     */
    E *GetData() noexcept { return events.get(); }

    /**
     * \brief Return a const pointer to the event array buffer.
     */
    E const *GetData() const noexcept { return events.get(); }
};

/**
 * \brief Memory pool holding event arrays for reuse.
 *
 * In other words, a free list of EventArray instances that automatically
 * allocates additional instances on demand.
 *
 * Note that behavior is undefined unless all checked out buffers are released
 * before the pool is destroyed.
 *
 * \tparam E the event type
 */
template <typename E> class EventArrayPool {
    std::size_t const bufferSize;

    std::mutex mutex;
    std::vector<std::unique_ptr<EventArray<E>>> buffers;

    std::unique_ptr<EventArray<E>> MakeBuffer() {
        return std::make_unique<EventArray<E>>(bufferSize);
    }

  public:
    /**
     * \brief Construct with array size and initial count.
     *
     * \param size capacity of the \c EventArray instances
     * \param initialCount number of \c EventArray instances to pre-allocate
     */
    explicit EventArrayPool(std::size_t size, std::size_t initialCount = 0)
        : bufferSize(size) {
        buffers.reserve(initialCount);
        for (std::size_t i = 0; i < initialCount; ++i) {
            buffers.emplace_back(MakeBuffer());
        }
    }

    /**
     * \brief Obrain an event array for use.
     *
     * The returned shared pointer has a deleter that will automatically return
     * (check in) the event array back to this pool.
     *
     * Note that all checked out buffers must be released (by allowing all
     * shared pointers to be destroyed) before the pool is destroyed.
     *
     * \return shared pointer to the event array
     */
    std::shared_ptr<EventArray<E>> CheckOut() {
        std::unique_ptr<EventArray<E>> uptr;

        {
            std::lock_guard<std::mutex> hold(mutex);
            if (!buffers.empty()) {
                uptr = std::move(buffers.back());
                buffers.pop_back();
            }
        }

        if (!uptr) {
            uptr = MakeBuffer();
        }

        uptr->SetSize(0);

        return {uptr.release(), [this](auto ptr) {
                    if (ptr == nullptr)
                        return;

                    std::lock_guard<std::mutex> hold(mutex);
                    buffers.emplace_back(std::unique_ptr<EventArray<E>>(ptr));
                }};
    }
};

/**
 * \brief Processor transforming event arrays to individual events.
 *
 * \tparam E the event type
 * \tparam D downstream processor type
 */
template <typename E, typename D> class EventArrayDemultiplexer {
    D downstream;

  public:
    /**
     * \brief The event array type.
     */
    using EventArrayType = std::shared_ptr<EventArray<E>>;

    /**
     * Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit EventArrayDemultiplexer(D &&downstream)
        : downstream(std::move(downstream)) {}

    /** \brief Processor interface */
    void HandleEvent(EventArrayType const &eventArray) noexcept {
        std::for_each_n(
            eventArray->GetData(), eventArray->GetSize(),
            [&](auto const &event) { downstream.HandleEvent(event); });
    }

    /** \brief Processor interface */
    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

/**
 * \brief A pseudo-processor that buffers events.
 *
 * This receives events of type \c E from upstream like a normal processor, but
 * stores them in a buffer. By calling PumpDownstream() on a different thread,
 * the buffered events can be sent downstream on that thread.
 *
 * Usually \c E should be EventArray in order to reduce overhead.
 *
 * \tparam E the event type
 * \tparam D downstream processor type
 */
template <typename E, typename D> class EventBuffer {
    std::mutex mutex;
    std::condition_variable hasItemCondition; // item = event or end

    std::queue<E> queue;
    bool streamEnded = false;
    std::exception_ptr queued_error;

    D downstream;

  public:
    /**
     * \brief Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit EventBuffer(D &&downstream) : downstream(std::move(downstream)) {}

    /** \brief Processor interface */
    void HandleEvent(E const &event) noexcept {
        {
            std::scoped_lock lock(mutex);
            if (streamEnded)
                return;

            try {
                queue.push(event);
            } catch (std::exception const &) {
                streamEnded = true;
                queued_error = std::current_exception();
            }
        }
        hasItemCondition.notify_one();
    }

    /** \brief Processor interface */
    void HandleEnd(std::exception_ptr error) noexcept {
        {
            std::scoped_lock lock(mutex);
            if (streamEnded)
                return;

            streamEnded = true;
            queued_error = error;
        }
        hasItemCondition.notify_one();
    }

    /**
     * \brief Send buffered events downstream on the caller's thread.
     *
     * This function blocks until the upstream has singaled the end of stream
     * and all events have been emitted downstream.
     */
    void PumpDownstream() noexcept {
        // Removal from the queue only takes place here; because std::deque
        // does not move elements, we can safely access the contents of
        // queue.front() with the mutex unlocked.

        std::unique_lock lock(mutex);

        for (;;) {
            while (queue.empty() && !streamEnded)
                hasItemCondition.wait(lock);

            bool isEnd = queue.empty();

            std::exception_ptr error;
            E const *event = nullptr;

            if (isEnd)
                std::swap(error, queued_error);
            else
                event = &queue.front();

            lock.unlock();

            if (isEnd) {
                downstream.HandleEnd(error);
                return;
            }

            downstream.HandleEvent(*event);

            lock.lock();

            queue.pop();
        }
    }
};

} // namespace flimevt
