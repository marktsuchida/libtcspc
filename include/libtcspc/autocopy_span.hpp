/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "span.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <type_traits>
#include <vector>

namespace tcspc {

// Design note (in addition to rationale in the doc comment below): We could
// use std::shared_ptr<T const> to handle large buffers in events. However,
// this is problematic due to the potentially high frequency of some of these
// events (especially histogram_event and element_histogram_event) and the high
// overhead (due to atomic operations) of creating and destroying shared_ptr
// copies. The autocopy_span is designed for single-threaded (or externally
// synchronized) use only, so generally does not have this issue.

// Design note: For std::span, copies are shallow (do not copy the bytes), and
// accordingly const is shallow (you can mutate the bytes referenced by a const
// span); there is no comparison to prevent misuse. For autocopy_span, copies
// are deep, and so is const and comparison. See https://wg21.link/P1085.

/**
 * \brief Like \c std::span, but allocates new memory when copied.
 *
 * \ingroup misc
 *
 * Instances hold a span of \c T[] memory, which may or may not be owned by the
 * instance. Copying any instance copies the data, and the result is an
 * instance that owns the memory for its data.
 *
 * Moving does not copy the data. This is safe, because moving out requires
 * having a mutable reference to the object to begin with.
 *
 * These semantics are designed for use as a field in an event. The idea is to
 * allow events to contain large zero-copy buffers while still maintaining
 * copyability (and regularity) of event objects. Regularity is extremely
 * valuable for easy testing and quick informal usage.
 *
 * To be efficient, consumers of events containing autocopy_span should
 * generally take care not to make unnecessary copies.
 *
 * One semantic difference from \c std::span is that the data referenced by
 * autocopy_span is const when the autocopy_span itself is \c const, regardless
 * of whether \c T is \c const.
 *
 * \c T must be copyable.
 *
 * \tparam T the array element type (typically numeric)
 */
template <typename T> class autocopy_span {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    std::unique_ptr<T[]> m;
    span<T> s;

  public:
    /**
     * \brief Construct an empty span.
     */
    autocopy_span() noexcept = default;

    /**
     * \brief Construct a non-owning span.
     *
     * \param span a span that will outlive this instance, owned by the
     * creating code
     */
    explicit autocopy_span(span<T> span) noexcept : s(span) {}

    /**
     * \brief Copy-construct.
     *
     * \param other the source instance
     */
    autocopy_span(autocopy_span const &other)
        : m([&s = other.s] {
              using TMut = std::remove_cv_t<T>;
              // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
              std::unique_ptr<TMut[]> r(new TMut[s.size()]);
              std::copy(s.begin(), s.end(), r.get());
              return r;
          }()),
          s(m.get(), other.s.size()) {}

    /**
     * \brief Move-construct.
     *
     * The new instance will adopt the span and backing memory (if owned by the
     * source).
     *
     * As with any move, the source instance should not be used after the move,
     * other than by assigning a new value.
     *
     * \param other the source instance
     */
    autocopy_span(autocopy_span &&other) noexcept = default;

    /** \brief Copy assignment operator. */
    auto operator=(autocopy_span const &other) -> autocopy_span & {
        autocopy_span t(other);
        std::swap(*this, t);
        return *this;
    }

    /** \brief Move assignment operator. */
    auto operator=(autocopy_span &&other) noexcept
        -> autocopy_span & = default;

    ~autocopy_span() = default; // Rule of 5

    /**
     * \brief Get the span represented.
     *
     * \return the span
     */
    auto as_span() const noexcept -> span<T const> { return s; }

    /**
     * \brief Get the span represented.
     *
     * \return the span
     */
    auto as_span() noexcept -> span<T> { return s; }

    /**
     * \brief Implicit conversion to span.
     */
    operator span<T const>() const noexcept { return s; }

    /**
     * \brief Implicit conversion to span.
     */
    operator span<T>() noexcept { return s; }

    /** \brief Equiality comparison operator. */
    friend auto operator==(autocopy_span const &lhs,
                           autocopy_span const &rhs) noexcept -> bool {
        return lhs.s.size() == rhs.s.size() &&
               std::equal(lhs.s.begin(), lhs.s.end(), rhs.s.begin());
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(autocopy_span const &lhs,
                           autocopy_span const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }
};

/** \brief Deduction guide for std::vector. */
template <typename T> autocopy_span(std::vector<T>) -> autocopy_span<T>;

/** \brief Deduction guide for std::array. */
template <typename T, std::size_t N>
autocopy_span(std::array<T, N>) -> autocopy_span<T>;

// C++20: static_assert(std::regular<autocopy_span<int>>);

} // namespace tcspc
