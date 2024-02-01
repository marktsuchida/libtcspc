/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <new>
#include <utility>

namespace tcspc::internal {

// Equivalent of std::queue<T> (i.e., std::queue<T, std::deque<T>>) but backed
// by a single array used as a ring buffer and enlarged as needed (which should
// result in better locality of reference and fewer allocations/deallocations
// than std::deque). This is intended for event buffering where the
// steady-state buffer capacity is (expected to be) bounded.
//
// This is similar to std::queue<T, boost::devector<T>>, but I don't want to
// have Boost as a dependency. Implementing a devector equivalent that can be
// used with std::queue would be much more work (iterators, etc.).
//
// For exception safety, T should be no-throw move-constructible.
template <typename T> class vector_queue {
    T *ptr;    // nullptr iff zero capacity
    T *endptr; // nullptr iff zero capacity
    T *head;   // Never equal to endptr unless zero capacity
    T *tail;   // Never equal to endptr unless zero capacity

    // We use std::allocator for implementation convenience, even though we
    // don't currently allow allocator customization.
    using alloctraits = std::allocator_traits<std::allocator<T>>;

    [[nodiscard]] constexpr auto is_full() const noexcept -> bool {
        return !ptr || tail + 1 == head || (tail + 1 == endptr && head == ptr);
    }

    static auto compute_enlarged_cap(std::size_t oldcap) -> std::size_t {
        auto alloc = std::allocator<T>();
        auto max_size = alloctraits::max_size(alloc);
        if (oldcap == max_size)
            throw std::bad_alloc();
        // 1.5x plus a tiny bit to ensure 0 is enlarged to > 0
        std::size_t newcap = (oldcap + 2) / 2 * 3;
        if (newcap < oldcap || newcap > max_size)
            newcap = max_size;
        return newcap;
    }

    void expand_cap() {
        std::size_t siz = size();
        std::size_t newcap =
            compute_enlarged_cap(as_unsigned(std::distance(ptr, endptr)));
        auto alloc = std::allocator<T>();
        auto dtor = [&](T &v) { alloctraits::destroy(alloc, &v); };

        T *newptr = alloctraits::allocate(alloc, newcap);

        if (head > tail) {
            std::uninitialized_move(head, endptr, newptr);
            std::uninitialized_move(ptr, tail,
                                    newptr + std::distance(head, endptr));
            std::for_each(head, endptr, dtor);
            std::for_each(ptr, tail, dtor);
        } else {
            std::uninitialized_move(head, tail, newptr);
            std::for_each(head, tail, dtor);
        }
        alloctraits::deallocate(alloc, ptr,
                                as_unsigned(std::distance(ptr, endptr)));

        ptr = newptr;
        endptr = newptr + newcap;
        head = newptr;
        tail = newptr + siz;
    }

  public:
    ~vector_queue() {
        auto alloc = std::allocator<T>();
        auto dtor = [&](T &v) { alloctraits::destroy(alloc, &v); };

        if (head > tail) {
            std::for_each(head, endptr, dtor);
            std::for_each(ptr, tail, dtor);
        } else {
            std::for_each(head, tail, dtor);
        }

        alloctraits::deallocate(alloc, ptr,
                                as_unsigned(std::distance(ptr, endptr)));
    }

    vector_queue() noexcept
        : ptr(nullptr), endptr(nullptr), head(nullptr), tail(nullptr) {}

    vector_queue(vector_queue const &other) {
        std::size_t cap = other.size() > 0 ? other.size() + 1 : 0;
        if (cap > 0) {
            auto alloc = std::allocator<T>();
            ptr = alloctraits::allocate(alloc, cap);
            if (other.head > other.tail) {
                std::uninitialized_copy(other.head, other.endptr, ptr);
                std::uninitialized_copy(
                    other.ptr, other.tail,
                    ptr + std::distance(other.head, other.endptr));
            } else {
                std::uninitialized_copy(other.head, other.tail, ptr);
            }
        } else {
            ptr = nullptr;
        }
        endptr = ptr + cap;
        head = ptr;
        tail = ptr + other.size();
    }

    vector_queue(vector_queue &&other) noexcept
        : ptr(std::exchange(other.ptr, nullptr)),
          endptr(std::exchange(other.endptr, nullptr)),
          head(std::exchange(other.head, nullptr)),
          tail(std::exchange(other.tail, nullptr)) {}

    auto operator=(vector_queue const &rhs) -> vector_queue & {
        vector_queue t(rhs);
        swap(t);
        return *this;
    }

    auto operator=(vector_queue &&rhs) noexcept -> vector_queue & {
        vector_queue t(std::move(rhs));
        swap(t);
        return *this;
    }

    [[nodiscard]] auto empty() const noexcept -> bool { return head == tail; }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        if (head > tail)
            return as_unsigned(std::distance(head, endptr) +
                               std::distance(ptr, tail));
        return as_unsigned(std::distance(head, tail));
    }

    auto front() -> T & {
        assert(head != tail);
        return *head;
    }

    auto front() const -> T const & {
        assert(head != tail);
        return *head;
    }

    auto back() -> T & {
        assert(head != tail);
        if (tail == ptr)
            return *(endptr - 1);
        return *(tail - 1);
    }

    auto back() const -> T const & {
        assert(head != tail);
        if (tail == ptr)
            return *(endptr - 1);
        return *(tail - 1);
    }

    void pop() {
        assert(head != tail);
        auto alloc = std::allocator<T>();
        alloctraits::destroy(alloc, head);
        ++head;
        if (head == endptr)
            head = ptr;
    }

    void push(T const &value) {
        if (is_full())
            expand_cap();
        auto alloc = std::allocator<T>();
        alloctraits::construct(alloc, tail, value);
        ++tail;
        if (tail == endptr)
            tail = ptr;
    }

    void push(T &&value) {
        if (is_full())
            expand_cap();
        auto alloc = std::allocator<T>();
        alloctraits::construct(alloc, tail, std::move(value));
        ++tail;
        if (tail == endptr)
            tail = ptr;
    }

    void swap(vector_queue &other) noexcept {
        using std::swap;
        swap(ptr, other.ptr);
        swap(endptr, other.endptr);
        swap(head, other.head);
        swap(tail, other.tail);
    }

    // Not in std::queue interface
    template <typename F>
    void for_each(F func) noexcept(noexcept(func(std::declval<T &>()))) {
        if (head <= tail) {
            std::for_each(head, tail, func);
        } else {
            std::for_each(head, endptr, func);
            std::for_each(ptr, tail, func);
        }
    }

    template <typename F>
    void for_each(F func) const noexcept(noexcept(func(std::declval<T &>()))) {
        if (head <= tail) {
            std::for_each(head, tail, func);
        } else {
            std::for_each(head, endptr, func);
            std::for_each(ptr, tail, func);
        }
    }
};

} // namespace tcspc::internal
