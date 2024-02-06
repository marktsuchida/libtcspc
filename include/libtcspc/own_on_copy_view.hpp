/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <ostream>
#include <type_traits>
#include <vector>

namespace tcspc {

// Design note (in addition to rationale in the doc comment below): We could
// use std::shared_ptr<T const> to handle large buffers in events. However,
// this is problematic due to the potentially high frequency of some of these
// events (especially histogram_event and element_histogram_event) and the high
// overhead (due to atomic operations) of creating and destroying shared_ptr
// copies. The own_on_copy_view is designed for single-threaded (or externally
// synchronized) use only, so generally does not have this issue.

// Design note: For std::span, copies are shallow (do not copy the bytes), and
// accordingly const is shallow (you can mutate the bytes referenced by a const
// span of non-const T); there is no comparison to prevent misuse. For
// own_on_copy_view, copies are deep, and so is const and comparison. See
// https://wg21.link/P1085.

/**
 * \brief A reference to a contiguous sequence of \p T that creates an owning
 * copy of the sequence upon copying.
 *
 * \ingroup misc
 *
 * The purose of this type is to represent large blocks of memory that should
 * be passed by reference in production code, while also having regular (i.e.,
 * default-initializabile, copyabile, and equality comparable) value semantics
 * that make testing easier.
 *
 * Instances hold a \ref span of a copyable type \c T, whose storage may or may
 * not be owned by the instance. Copying any instance copies the data, and the
 * result is an instance that owns the memory for its data.
 *
 * Producers of own_on_copy_view normally create the instance from a \ref span
 * of memory owned by the producer and pass the instance to a consumer
 * function, guaranteeing that the producer-owned referenced memory survives
 * the call. (It is usually inappropriate to use own_on_copy_view as a function
 * return value.) Producers should only pass to consumers a const reference to
 * the instance (using \c std::as_const as needed).
 *
 * Consumers of own_on_copy_view normally recieve an instance as a function
 * parameter (which should be a const lvalue reference) and read the referred
 * data only within the duration of the call.
 *
 * Moving an own_on_copy_view does not copy the data. This is safe, provided
 * the above usage pattern, because moving out requires having a mutable
 * reference to the instance.
 *
 * Consistant with value semantics, the data referenced by own_on_copy_view is
 * const when the own_on_copy_view itself is \c const, regardless of whether or
 * not \c T is \c const.
 */
template <typename T> class own_on_copy_view {
    // We cannot use std::vector for m, because `T` may be const.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    std::unique_ptr<T[]> m;
    span<T> s;

  public:
    /**
     * \brief Construct an empty instance.
     */
    own_on_copy_view() noexcept = default;

    /**
     * \brief Construct a non-owning view.
     *
     * The caller must guarantee that the sequence of objects referred to by \p
     * span outlives the constructed instance.
     */
    explicit own_on_copy_view(span<T> span) noexcept : s(span) {}

    /**
     * \brief Copy-construct.
     */
    own_on_copy_view(own_on_copy_view const &other)
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
     */
    own_on_copy_view(own_on_copy_view &&other) noexcept = default;

    /** \brief Copy assignment operator. */
    auto operator=(own_on_copy_view const &other) -> own_on_copy_view & {
        own_on_copy_view t(other);
        std::swap(*this, t);
        return *this;
    }

    /** \brief Move assignment operator. */
    auto operator=(own_on_copy_view &&other) noexcept
        -> own_on_copy_view & = default;

    ~own_on_copy_view() = default; // Rule of 5

    /**
     * \brief Return the span of referenced objects.
     */
    auto as_span() const noexcept -> span<T const> { return s; }

    /**
     * \brief Return the span of referenced objects.
     */
    auto as_span() noexcept -> span<T> { return s; }

    /**
     * \brief Equiality comparison operator.
     *
     * Returns true if \p lhs and \p rhs refer to sequences of equal size
     * containing equal objects.
     */
    friend auto operator==(own_on_copy_view const &lhs,
                           own_on_copy_view const &rhs) noexcept -> bool {
        return lhs.s.size() == rhs.s.size() &&
               std::equal(lhs.s.begin(), lhs.s.end(), rhs.s.begin());
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(own_on_copy_view const &lhs,
                           own_on_copy_view const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /**
     * \brief Stream insertion operator.
     *
     * Enabled if \p T has a stream insertion operator or if \p T is \c
     * std::byte.
     */
    friend auto operator<<(std::ostream &stream, own_on_copy_view const &view)
        -> std::ostream & {
        static constexpr std::size_t num_to_print = 32;
        auto const size = view.s.size();
        stream << "own_on_copy_view(size=" << size;
        if constexpr (std::is_same_v<std::remove_cv_t<T>, std::byte>) {
            for (std::size_t i = 0; i < std::min(size, num_to_print - 1); ++i)
                stream << ", " << std::to_integer<int>(view.s[i]);
            if (size > num_to_print)
                stream << ", ...";
            if (size >= num_to_print)
                stream << ", " << std::to_integer<int>(view.s[size - 1]);
        } else {
            for (std::size_t i = 0; i < std::min(size, num_to_print - 1); ++i)
                stream << ", " << view.s[i];
            if (size > num_to_print)
                stream << ", ...";
            if (size >= num_to_print)
                stream << ", " << view.s[size - 1];
        }
        return stream << ')';
    }
};

/** \brief Deduction guide for std::vector. */
template <typename T> own_on_copy_view(std::vector<T>) -> own_on_copy_view<T>;

/** \brief Deduction guide for std::array. */
template <typename T, std::size_t N>
own_on_copy_view(std::array<T, N>) -> own_on_copy_view<T>;

// C++20: static_assert(std::regular<own_on_copy_view<int>>);

} // namespace tcspc
