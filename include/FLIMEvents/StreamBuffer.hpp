#pragma once

#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

// Fixed-capacity reusable memory to hold a bunch of photon events
// E = event data type (plain struct or integer)
template <typename E> class EventBuffer {
    std::size_t const capacity;
    std::size_t size;
    std::unique_ptr<E[]> events;

  public:
    explicit EventBuffer(std::size_t capacity)
        : capacity(capacity), size(0), events(new E[capacity]) {}

    std::size_t GetCapacity() const noexcept { return capacity; }

    std::size_t GetSize() const noexcept { return size; }

    void SetSize(std::size_t size) noexcept { this->size = size; }

    E *GetData() noexcept { return events.get(); }

    E const *GetData() const noexcept { return events.get(); }
};

template <typename E> class EventBufferPool {
    std::size_t const bufferSize;

    std::mutex mutex;
    std::vector<std::unique_ptr<EventBuffer<E>>> buffers;

    std::unique_ptr<EventBuffer<E>> MakeBuffer() {
        return std::make_unique<EventBuffer<E>>(bufferSize);
    }

  public:
    explicit EventBufferPool(std::size_t size, std::size_t initialCount = 0)
        : bufferSize(size) {
        buffers.reserve(initialCount);
        for (std::size_t i = 0; i < initialCount; ++i) {
            buffers.emplace_back(MakeBuffer());
        }
    }

    // Obtain a buffer for use. Returns a shared pointer which automatically
    // checks in the buffer when the calling code is finished with it.
    // Note: all checked out buffers must be released before the pool is
    // destroyed.
    std::shared_ptr<EventBuffer<E>> CheckOut() {
        std::unique_ptr<EventBuffer<E>> uptr;

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
                    if (!ptr)
                        return;

                    std::lock_guard<std::mutex> hold(mutex);
                    buffers.emplace_back(std::unique_ptr<EventBuffer<E>>(ptr));
                }};
    }
};

// A thread-safe queue of EventBuffer<E>, with non-blocking enqueue and
// blocking dequeue.
template <typename E> class EventStream {
    std::mutex mutex;
    std::condition_variable queueNotEmptyCondition;
    std::deque<std::shared_ptr<EventBuffer<E>>> queue;
    std::exception_ptr exception;

  public:
    // Sending a null will terminate the stream
    void Send(std::shared_ptr<EventBuffer<E>> buffer) {
        {
            std::lock_guard<std::mutex> hold(mutex);
            queue.emplace_back(buffer);
        }
        queueNotEmptyCondition.notify_one();
    }

    void SendException(std::exception_ptr e) {
        {
            std::lock_guard<std::mutex> hold(mutex);
            queue.emplace_back(std::shared_ptr<EventBuffer<E>>());
            exception = e;
        }
    }

    // A null shared pointer return value indicates that the stream has been
    // terminated. Subsequent calls will block forever.
    // Throws if upstream sent an exception.
    std::shared_ptr<EventBuffer<E>> ReceiveBlocking() {
        std::unique_lock<std::mutex> lock(mutex);
        while (queue.empty()) {
            queueNotEmptyCondition.wait(lock);
        }
        auto ret = queue.front();
        queue.pop_front();
        if (!ret && exception) {
            std::rethrow_exception(exception);
        }
        return ret;
    }
};
