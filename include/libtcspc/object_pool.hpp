/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "introspect.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tcspc {

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
        if (max_count == 0)
            throw std::invalid_argument(
                "object_pool max_count must not be zero");
        if (initial_count > max_count)
            throw std::invalid_argument(
                "object_pool initial_count must not be greater than max_count");
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
    auto try_ckeck_out() -> std::shared_ptr<T> {
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
    explicit dereference_pointer(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "dereference_pointer");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Pointer const &event_ptr) { downstream.handle(*event_ptr); }

    void flush() { downstream.flush(); }
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

} // namespace tcspc
