#pragma once

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace flimevt {

// Fixed-capacity reusable memory to hold a bunch of photon events
// E = event data type (plain struct or integer)
template <typename E> class EventArray {
    std::size_t const capacity;
    std::size_t size;
    std::unique_ptr<E[]> events;

  public:
    explicit EventArray(std::size_t capacity)
        : capacity(capacity), size(0), events(new E[capacity]) {}

    std::size_t GetCapacity() const noexcept { return capacity; }

    std::size_t GetSize() const noexcept { return size; }

    void SetSize(std::size_t size) noexcept { this->size = size; }

    E *GetData() noexcept { return events.get(); }

    E const *GetData() const noexcept { return events.get(); }
};

template <typename E> class EventArrayPool {
    std::size_t const bufferSize;

    std::mutex mutex;
    std::vector<std::unique_ptr<EventArray<E>>> buffers;

    std::unique_ptr<EventArray<E>> MakeBuffer() {
        return std::make_unique<EventArray<E>>(bufferSize);
    }

  public:
    explicit EventArrayPool(std::size_t size, std::size_t initialCount = 0)
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
    // (We use shared_ptr rather than unique_ptr so that the custom deleter is
    // type-erased.)
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

template <typename E, typename D> class EventArrayDemultiplexer {
    D downstream;

  public:
    using EventArrayType = std::shared_ptr<EventArray<E>>;

    explicit EventArrayDemultiplexer(D &&downstream)
        : downstream(std::move(downstream)) {}

    void HandleEvent(EventArrayType const &eventArray) noexcept {
        std::for_each_n(
            eventArray->GetData(), eventArray->GetSize(),
            [&](auto const &event) { downstream.HandleEvent(event); });
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

// Buffer for single-type event stream
template <typename E, typename D> class EventBuffer {
    std::mutex mutex;
    std::condition_variable hasItemCondition; // item = event or end

    std::queue<E> queue;
    bool streamEnded = false;
    std::exception_ptr queued_error;

    D downstream;

  public:
    explicit EventBuffer(D &&downstream) : downstream(std::move(downstream)) {}

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

    // To be called on processing thread. Blocks until end is received.
    void PumpDownstream() noexcept {
        // Removal from the queue only takes place here; because std::deque does
        // not move elements, we can safely access the contents of queue.front()
        // with the mutex unlocked.

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
