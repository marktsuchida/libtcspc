/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "errors.hpp"
#include "introspect.hpp"
#include "move_only_any.hpp"
#include "processor_traits.hpp"
#include "span.hpp"

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

/**
 * \brief Value-semantic container for array data allowing use of custom
 * storage.
 *
 * \ingroup buckets
 *
 * A bucket may be used as an event type (as in the output of `tcspc::batch` or
 * `tcspc::read_binary_stream`), or as a field in another event (as in the
 * histogram events).
 *
 * Bucket instances are obtained from a `tcspc::bucket_source` (see \ref
 * bucket-sources).
 *
 * A bucket implements, among other things, an interface similar to
 * `tcspc::span`, allowing it to be treated as a contiguous container of \p T
 * objects.
 *
 * Copying a bucket copies the data into newly allocated memory. This should be
 * avoided in production code, but is convenient for testing of processors that
 * emit buckets.
 *
 * Moving a bucket transfers both its data and underlying storage to the
 * destination.
 *
 * A bucket holds a _storage_, which can carry ownership or information about
 * the bucket's underlying storage. The type of the storage is dependent on the
 * bucket source (it is stored in the bucket in a type-erased form). Where
 * supported by the bucket source, the storage has a known type and can be
 * observed or extracted from a bucket, recovering direct access to the
 * underlying storage.
 *
 * A default-constructed bucket is empty and has no observable or extractable
 * storage.
 *
 * Comparing two buckets for equality (`==`) or inequality (`!=`) returns
 * whether the data is equal or not (note that this differs from
 * `tcspc::span`). Together with the copy behavior, this makes `bucket<T>` a
 * regular type.
 *
 * Processors emitting buckets are typically constructed by passing in the
 * bucket source. They should emit buckets (or events containing buckets) by
 * const reference when letting the downstream observe the bucket contents
 * before the processor finishes filling them. Finished buckets should be
 * emitted by rvalue reference (`std::move`) so that the downstream processor
 * can extract the storage if it so desires. Processors that emit a sequence of
 * buckets in these ways should document the semantics of the sequence, and
 * (usually) obtain buckets from the provided bucket source in the exact order
 * in which they are emitted.
 *
 * Buckets of const elements (\p T is const-qualified) are sometimes used to
 * construct views of const data, in contexts where the ability to extract
 * storage is not relevant. In this case the bucket is used as a read-only data
 * handle similar to `tcspc::span`, but with the convenience of a regular type.
 *
 * \tparam T element type of the data array carried by the bucket
 */
template <typename T> class bucket {
    struct owning_storage {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        std::unique_ptr<T[]> p;

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        explicit owning_storage(std::unique_ptr<T[]> ptr)
            : p(std::move(ptr)) {}
    };

    struct view_storage {
        bucket const *original;
    };

    span<T> s;
    internal::move_only_any store;

  public:
    bucket() noexcept = default;

    /**
     * \brief Construct a bucket referenceing a \p span and holding \p storage.
     *
     * This constructor is normally used by bucket sources.
     *
     * \tparam S storage type (deduced)
     */
    template <typename S>
    explicit bucket(span<T> span, S &&storage)
        : s(span), store(std::forward<S>(storage)) {}

    ~bucket() = default;

    /** \brief Copy constructor. */
    bucket(bucket const &other)
        : store([&s = other.s] {
              using TMut = std::remove_cv_t<T>;
              // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
              std::unique_ptr<TMut[]> r(new TMut[s.size()]);
              std::copy(s.begin(), s.end(), r.get());
              return internal::move_only_any(
                  std::in_place_type<owning_storage>, std::move(r));
          }()) {
        T *ptr = internal::move_only_any_cast<owning_storage>(&store)->p.get();
        s = {ptr, other.s.size()};
    }

    /** \brief Move constructor. */
    bucket(bucket &&other) noexcept = default;

    /** \brief Copy assignment operator. */
    auto operator=(bucket const &other) -> bucket & {
        bucket t(other);
        std::swap(*this, t);
        return *this;
    }

    /** \brief Move assignment operator. */
    auto operator=(bucket &&other) noexcept -> bucket & = default;

    // Equivalents of most of span's members follow. But many of the member
    // functions are duplicated to work with a const bucket (because a const
    // bucket has read-only data, unlike a const span).

    /** \brief Element type. */
    using element_type = typename span<T>::element_type;
    /** \brief Value type. */
    using value_type = typename span<T>::value_type;
    /** \brief Size type. */
    using size_type = typename span<T>::size_type;
    /** \brief Difference type. */
    using difference_type = typename span<T>::difference_type;
    /** \brief Element pointer type. */
    using pointer = typename span<T>::pointer;
    /** \brief Element const pointer type. */
    using const_pointer = typename span<T>::const_pointer;
    /** \brief Element reference type. */
    using reference = typename span<T>::reference;
    /** \brief Element const reference type. */
    using const_reference = typename span<T>::const_reference;
    /** \brief Iterator type. */
    using iterator = typename span<T>::iterator;
    /** \brief Const iterator type. */
    using const_iterator = typename span<T const>::iterator;
    /** \brief Reverse iterator type. */
    using reverse_iterator = typename span<T>::reverse_iterator;
    /** \brief Const reverse iterator type. */
    using const_reverse_iterator = typename span<T const>::reverse_iterator;

    /** \brief Return an iterator to the beginning. */
    [[nodiscard]] auto begin() noexcept -> iterator { return s.begin(); }

    /** \brief Return an iterator to the beginning. */
    [[nodiscard]] auto cbegin() const noexcept -> const_iterator {
        return span<T const>(s).begin();
    }

    /** \brief Return an iterator to the beginning. */
    [[nodiscard]] auto begin() const noexcept -> const_iterator {
        return cbegin();
    }

    /** \brief Return an iterator to the end. */
    [[nodiscard]] auto end() noexcept -> iterator { return s.end(); }

    /** \brief Return an iterator to the end. */
    [[nodiscard]] auto cend() const noexcept -> const_iterator {
        return span<T const>(s).end();
    }

    /** \brief Return an iterator to the end. */
    [[nodiscard]] auto end() const noexcept -> const_iterator {
        return cend();
    }

    /** \brief Return a reverse iterator to the beginning. */
    [[nodiscard]] auto rbegin() noexcept -> reverse_iterator {
        return s.rbegin();
    }

    /** \brief Return a reverse iterator to the beginning. */
    [[nodiscard]] auto crbegin() const noexcept -> const_reverse_iterator {
        return span<T const>(s).rbegin();
    }

    /** \brief Return a reverse iterator to the beginning. */
    [[nodiscard]] auto rbegin() const noexcept -> const_reverse_iterator {
        return crbegin();
    }

    /** \brief Return a reverse iterator to the end. */
    [[nodiscard]] auto rend() noexcept -> reverse_iterator { return s.rend(); }

    /** \brief Return a reverse iterator to the end. */
    [[nodiscard]] auto crend() const noexcept -> const_reverse_iterator {
        return span<T const>(s).rend();
    }

    /** \brief Return a reverse iterator to the end. */
    [[nodiscard]] auto rend() const noexcept -> const_reverse_iterator {
        return crend();
    }

    /** \brief Return the first element. */
    [[nodiscard]] auto front() -> reference { return s.front(); }

    /** \brief Return the first element. */
    [[nodiscard]] auto front() const -> const_reference {
        return span<T const>(s).front();
    }

    /** \brief Return the last element. */
    [[nodiscard]] auto back() -> reference { return s.back(); }

    /** \brief Return the last element. */
    [[nodiscard]] auto back() const -> const_reference {
        return span<T const>(s).back();
    }

    /** \brief Return an element without bounds checking. */
    [[nodiscard]] auto operator[](size_type idx) -> reference {
        return s[idx];
    }

    /** \brief Return an element without bounds checking. */
    [[nodiscard]] auto operator[](size_type idx) const -> const_reference {
        return s[idx];
    }

    /** \brief Return an element with bounds checking. */
    [[nodiscard]] auto at(size_type pos) -> reference {
        if (pos >= size())
            throw std::out_of_range("bucket element index out of range");
        return s[pos];
    }

    /** \brief Return an element with bounds checking. */
    [[nodiscard]] auto at(size_type pos) const -> const_reference {
        if (pos >= size())
            throw std::out_of_range("bucket element index out of range");
        return s[pos];
    }

    /** \brief Return the address of the data.. */
    [[nodiscard]] auto data() noexcept -> pointer { return s.data(); }

    /** \brief Return the address of the data. */
    [[nodiscard]] auto data() const noexcept -> const_pointer {
        return s.data();
    }

    /** \brief Return the number of data elements in this bucket. */
    [[nodiscard]] auto size() const noexcept -> size_type { return s.size(); }

    /** \brief Return the size of this bucket's data in bytes. */
    [[nodiscard]] auto size_bytes() const noexcept -> size_type {
        return s.size_bytes();
    }

    /** \brief Return whether this bucket is empty. */
    [[nodiscard]] auto empty() const noexcept -> bool { return s.empty(); }

    /** \brief Return the span of the first \c count elements. */
    [[nodiscard]] auto first(size_type count) -> span<T> {
        return s.first(count);
    }

    /** \brief Return the span of the first \c count elements. */
    [[nodiscard]] auto first(size_type count) const -> span<T const> {
        return span<T const>(s).first(count);
    }

    /** \brief Return the span of the last \c count elements. */
    [[nodiscard]] auto last(size_type count) -> span<T> {
        return s.last(count);
    }

    /** \brief Return the span of the last \c count elements. */
    [[nodiscard]] auto last(size_type count) const -> span<T const> {
        return span<T const>(s).last(count);
    }

    /** \brief Return the span of the given range of elements. */
    [[nodiscard]] auto subspan(size_type offset,
                               size_type count = dynamic_extent) -> span<T> {
        return s.subspan(offset, count);
    }

    /** \brief Return the span of the given range of elements. */
    [[nodiscard]] auto
    subspan(size_type offset,
            size_type count = dynamic_extent) const -> span<T const> {
        return span<T const>(s).subspan(offset, count);
    }

    // End of span member equivalents.

    // We do not prevent access to storage of owning instance, but the value
    // inside the move_only_any will not be accessible because owning_storage
    // is a private type.

    /**
     * \brief Observe the underlying storage.
     *
     * \tparam S the storage type
     *
     * \return const reference to the storage object
     *
     * \throw std::bad_cast if \p S does not match the storage type of this
     * bucket
     */
    template <typename S> [[nodiscard]] auto storage() const -> S const & {
        auto *view = internal::move_only_any_cast<view_storage>(&store);
        if (view != nullptr)
            return view->original->template storage<S>();
        return internal::move_only_any_cast<S const &>(store);
    }

    /**
     * \brief Extract the underlying storage.
     *
     * The instance becomes an empty bucket after extraction. A bucket obtained
     * from a bucket source (that supports extraction) is required; extracting
     * the storage from a sub-bucket or a copied bucket is not supported
     * (cannot be done because the storage is a private type).
     *
     * \tparam S the storage type
     *
     * \return the storage object
     *
     * \throw std::bad_cast if \c S does not match the storage type of this
     * bucket
     */
    template <typename S> [[nodiscard]] auto extract_storage() -> S {
        s = {};
        S ret = internal::move_only_any_cast<S>(std::move(store));
        store = {};
        return ret;
    }

    /**
     * \brief Shrink the span of the bucket data.
     *
     * Mutates this bucket in place so that its span becomes a subspan of its
     * current span. There is no effect on the storage.
     *
     * Once shrunk, the excluded part of the data is no longer accessible
     * (except via a sub-bucket, byte bucket, or const bucket previously
     * created from this bucket).
     */
    void shrink(std::size_t start, std::size_t count = dynamic_extent) {
        s = s.subspan(start, count);
    }

    // In contrast to span, bucket equality is deep equality.

    /**
     * \brief Equality comparison operator.
     *
     * \return true if the two buckets contain equal data.
     */
    friend auto operator==(bucket const &lhs, bucket const &rhs) -> bool {
        return lhs.s.size() == rhs.s.size() &&
               std::equal(lhs.s.begin(), lhs.s.end(), rhs.s.begin());
    }

    /**
     * \brief Inequality comparison operator.
     *
     * \return true if the two buckets do not contain equal data.
     */
    friend auto operator!=(bucket const &lhs, bucket const &rhs) -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           bucket const &bkt) -> std::ostream & {
        static constexpr std::size_t num_to_print = 10;
        auto const size = bkt.s.size();
        stream << "bucket(size=" << size;
        if constexpr (std::is_same_v<std::remove_cv_t<T>, std::byte>) {
            for (std::size_t i = 0; i < std::min(size, num_to_print - 1); ++i)
                stream << ", " << std::to_integer<int>(bkt.s[i]);
            if (size > num_to_print)
                stream << ", ...";
            if (size >= num_to_print)
                stream << ", " << std::to_integer<int>(bkt.s[size - 1]);
        } else {
            for (std::size_t i = 0; i < std::min(size, num_to_print - 1); ++i)
                stream << ", " << bkt.s[i];
            if (size > num_to_print)
                stream << ", ...";
            if (size >= num_to_print)
                stream << ", " << bkt.s[size - 1];
        }
        return stream << ')';
    }
};

/**
 * \brief Create a `tcspc::bucket` referencing a span.
 *
 * \ingroup buckets
 *
 * This can be used when a bucket is needed but is only used to view existing
 * data and its storage is not important. The storage of the returned bucket
 * cannot be observed or extracted.
 *
 * \attention The caller is responsible for ensuring that the data in the span
 * outlives the returned bucket. Usually this means that the returned bucket
 * should only be published (e.g., by emitting to downstream) via const
 * reference and should never be returned or stored by the caller.
 *
 * \tparam T the bucket data element type
 *
 * \param s the span to wrap as an ad-hoc bucket
 */
template <typename T>
[[nodiscard]] auto ad_hoc_bucket(span<T> s) -> bucket<T> {
    struct ad_hoc_storage {};
    return bucket<T>(s, ad_hoc_storage{});
}

/**
 * \brief Abstract base class for polymorphic bucket sources.
 *
 * \ingroup bucket-sources
 *
 * Bucket source instances are handled via `std::shared_ptr`.
 *
 * \see \ref bucket-sources
 *
 * \tparam T the bucket data element type
 */
template <typename T> struct bucket_source {
    virtual ~bucket_source() = default;

    /**
     * \brief Create a bucket of \p size elements of type \p T.
     *
     * \attention Processors that use a bucket source must not create any
     * buckets during construction. Buckets should be created while handling
     * events (or flush) only. This is because we support use cases in which
     * the bucket source is fully configured only after the processing graph
     * has been built.
     */
    virtual auto bucket_of_size(std::size_t size) -> bucket<T> = 0;

    /**
     * \brief Return whether this bucket source is a sharable bucket source.
     *
     * A sharable bucket source supports the creation of shared views of
     * buckets via `shared_view_of()`.
     *
     * \note This function is overridden to return `true` for sharable bucket
     * sources. The default implementation returns `false`.
     */
    [[nodiscard]] virtual auto supports_shared_views() const noexcept -> bool {
        return false;
    }

    /**
     * \brief Create a shared view bucket that is a read-only view of the given
     * bucket but may outlive the original bucket.
     *
     * \note This function is only available for sharable bucket sources (see
     * `supports_shared_views()`). The default implementation throws
     * `std::logic_error`.
     *
     * When supported, this function creates a second bucket that shares
     * ownership of the underlying storage of the given \p bkt. A shared view
     * remains valid even if the original bucket is destroyed first.
     *
     * For this reason, it is safe to pass a shared view bucket by non-const
     * (rvalue) reference to other code, such as a downstream processor. This
     * allows move-semantic transmission of the shared view bucket, allowing
     * for, e.g., buffering without copying of the data.
     *
     * Shared views may only be created from original non-view buckets; they
     * cannot be created from existing shared views.
     *
     * Depending on the bucket source, storage extraction from a shared view
     * bucket may or may not be supported (even if the original buckets support
     * it).
     *
     * Sharability is an optional feature of bucket sources because managing
     * shared storage may have overhead.
     *
     * \param bkt the original bucket, which must have been returned by
     * `bucket_of_size()` of this bucket source (and not by a previous call to
     * `shared_view_of()`).
     *
     * \return A shared view bucket.
     */
    [[nodiscard]] virtual auto
    shared_view_of([[maybe_unused]] bucket<T> const &bkt) -> bucket<T const> {
        throw std::logic_error(
            "this bucket source does not support shared views");
    }
};

/**
 * \brief Bucket source using regular memory allocation.
 *
 * \ingroup bucket-sources
 *
 * This bucket source provides buckets whose underlying memory is allocated via
 * `new[]`. Extraction of the storage is supported and results in a
 * `std::unique_ptr<T[]>`.
 *
 * This bucket source is thread-safe: buckets (or their storage) may be created
 * and destroyed on multiple threads simultaneously. (Access to an individual
 * bucket is not thread-safe.)
 *
 * \tparam T the bucket data element type
 */
template <typename T>
class new_delete_bucket_source final : public bucket_source<T> {
    new_delete_bucket_source() = default;

  public:
    /** \brief Create an instance. */
    static auto create() -> std::shared_ptr<bucket_source<T>> {
        static std::shared_ptr<bucket_source<T>> instance(
            new new_delete_bucket_source());
        return instance;
    }

    /** \brief Implements bucket source requirement. */
    auto bucket_of_size(std::size_t size) -> bucket<T> override {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        std::unique_ptr<T[]> p(new T[size]);
        return bucket<T>{span(p.get(), size), std::move(p)};
    }
};

/**
 * \brief Sharable bucket source using regular memory allocation.
 *
 * \ingroup bucket-sources
 *
 * This bucket source provides buckets whose underlying memory is allocated via
 * `new[]`. Extraction of the storage is supported and results in a
 * `std::shared_ptr<T[]>`.
 *
 * This bucket source supports the creation of shared view buckets. Extraction
 * of storage from shared views is also supported.
 *
 * This bucket source is thread-safe: buckets (or their storage) may be created
 * and destroyed on multiple threads simultaneously. (Access to an individual
 * bucket is not thread-safe.)
 */
template <typename T>
class sharable_new_delete_bucket_source final : public bucket_source<T> {
    sharable_new_delete_bucket_source() = default;

  public:
    /** \brief Create an instance. */
    static auto create() -> std::shared_ptr<bucket_source<T>> {
        static std::shared_ptr<bucket_source<T>> instance(
            new sharable_new_delete_bucket_source());
        return instance;
    }

    /** \brief Implements bucket source requirement. */
    auto bucket_of_size(std::size_t size) -> bucket<T> override {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        std::shared_ptr<T[]> p(new T[size]);
        return bucket<T>{span(p.get(), size), std::move(p)};
    }

    /** \brief Implements sharable bucket source requirement. */
    [[nodiscard]] auto
    supports_shared_views() const noexcept -> bool override {
        return true;
    }

    /** \brief Implements sharable bucket source requirement. */
    [[nodiscard]] auto
    shared_view_of(bucket<T> const &bkt) -> bucket<T const> override {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        auto storage = bkt.template storage<std::shared_ptr<T[]>>();
        return bucket<T const>{span<T const>(bkt), std::move(storage)};
    }
};

/**
 * \brief Bucket source that reuses storage.
 *
 * \ingroup bucket-sources
 *
 * This bucket source provides buckets whose underlying memory is allocated via
 * `new[]`, but storage from destroyed buckets is reused for new buckets.
 * Extraction of the storage is not supported (cannot be done because the
 * storage is a private type).
 *
 * This bucket source is thread-safe: buckets (or their storage) may be created
 * and destroyed on multiple threads simultaneously. (Access to an individual
 * bucket is not thread-safe.)
 *
 * \tparam T the bucket element type
 *
 * \tparam Blocking if true, block when a new bucket is requested but the
 * number of outstanding buckets has reached the maximum count.
 *
 * \tparam ClearRecycled if true, bucket data elements will be destroyed when
 * the bucket (or its storage) is destroyed; otherwise elements may remain in
 * the recycled storage.
 */
template <typename T, bool Blocking = false, bool ClearRecycled = false>
class recycling_bucket_source final
    : public bucket_source<T>,
      public std::enable_shared_from_this<
          recycling_bucket_source<T, Blocking, ClearRecycled>> {
    std::mutex mutex;
    std::condition_variable not_empty_condition;
    std::size_t max_buckets;
    std::size_t bucket_count = 0;
    std::vector<std::unique_ptr<std::vector<T>>> recyclable;

    struct bucket_storage {
        std::shared_ptr<recycling_bucket_source> source;
        std::unique_ptr<std::vector<T>> storage;

        ~bucket_storage() {
            if (not source)
                return;

            assert(storage);

            if constexpr (ClearRecycled)
                storage->clear();

            {
                std::scoped_lock lock(source->mutex);
                source->recyclable.push_back(std::move(storage));
            }

            if constexpr (Blocking) {
                // Is it safe to wake up the source when this bucket_storage
                // might be the last thing holding on to it? Yes, because if
                // that is the case there is no thread waiting on the source.
                source->not_empty_condition.notify_one();
            }
        }

        bucket_storage(bucket_storage const &) = delete;
        auto operator=(bucket_storage const &) = delete;

        bucket_storage(bucket_storage &&) noexcept = default;
        auto
        operator=(bucket_storage &&) noexcept -> bucket_storage & = default;
    };

    explicit recycling_bucket_source(std::size_t max_bucket_count)
        : max_buckets(max_bucket_count) {}

  public:
    /**
     * \brief Create an instance.
     *
     * \param max_bucket_count the maximum number of buckets that can be
     * outstanding from this bucket source at any given time.
     */
    static auto create(
        std::size_t max_bucket_count = std::numeric_limits<std::size_t>::max())
        -> std::shared_ptr<bucket_source<T>> {
        return std::shared_ptr<recycling_bucket_source>(
            new recycling_bucket_source(max_bucket_count));
    }

    /**
     * \brief Implements bucket source requirement.
     *
     * This function will block if \p Blocking is true and the maximum bucket
     * count has been reached. It will then unblock when an outstanding bucket
     * is destroyed.
     *
     * \throw tcspc::buffer_overflow_error if \p Blocking is false and the
     * maximum bucket count has been reached.
     */
    auto bucket_of_size(std::size_t size) -> bucket<T> override {
        std::unique_ptr<std::vector<T>> p;
        {
            std::unique_lock lock(mutex);
            if (recyclable.empty() && bucket_count < max_buckets) {
                ++bucket_count;
            } else {
                if constexpr (Blocking) {
                    not_empty_condition.wait(
                        lock, [&] { return not recyclable.empty(); });
                } else if (recyclable.empty()) {
                    throw buffer_overflow_error(
                        "recycling bucket source exhausted");
                }
                p = std::move(recyclable.back());
                recyclable.pop_back();
            }
        }
        if (not p)
            p = std::make_unique<std::vector<T>>();
        p->resize(size);
        auto const spn = span(p->data(), p->size());
        return bucket<T>{
            spn, bucket_storage{this->shared_from_this(), std::move(p)}};
    }
};

/**
 * \brief Sharable bucket source that reuses storage.
 *
 * \ingroup bucket-sources
 *
 * This bucket source behaves identically to `tcspc::recycling_bucket_source`,
 * except that creation of shared view buckets is supported.
 *
 * Bucket storage is reused after all shared views are destroyed.
 */
template <typename T, bool Blocking = false, bool ClearRecycled = false>
class sharable_recycling_bucket_source final
    : public bucket_source<T>,
      public std::enable_shared_from_this<
          sharable_recycling_bucket_source<T, Blocking, ClearRecycled>> {
    std::mutex mutex;
    std::condition_variable not_empty_condition;
    std::size_t max_buckets;
    std::size_t bucket_count = 0;
    std::vector<std::unique_ptr<std::vector<T>>> recyclable;

    // Wrap storage in private type to forbid observation/extraction.
    struct bucket_storage {
        std::shared_ptr<std::vector<T>> storage; // With custom deleter.
    };

    explicit sharable_recycling_bucket_source(std::size_t max_bucket_count)
        : max_buckets(max_bucket_count) {}

  public:
    /**
     * \brief Create an instance.
     *
     * \copydetails tcspc::recycling_bucket_source::create()
     */
    static auto create(
        std::size_t max_bucket_count = std::numeric_limits<std::size_t>::max())
        -> std::shared_ptr<bucket_source<T>> {
        return std::shared_ptr<sharable_recycling_bucket_source>(
            new sharable_recycling_bucket_source(max_bucket_count));
    }

    /**
     * \brief Implements bucket source requirement.
     *
     * \copydetails tcspc::recycling_bucket_source::bucket_of_size()
     */
    auto bucket_of_size(std::size_t size) -> bucket<T> override {
        std::unique_ptr<std::vector<T>> p;
        {
            std::unique_lock lock(mutex);
            if (recyclable.empty() && bucket_count < max_buckets) {
                ++bucket_count;
            } else {
                if constexpr (Blocking) {
                    not_empty_condition.wait(
                        lock, [&] { return not recyclable.empty(); });
                } else if (recyclable.empty()) {
                    throw buffer_overflow_error(
                        "sharable recycling bucket source exhausted");
                }
                p = std::move(recyclable.back());
                recyclable.pop_back();
            }
        }
        if (not p)
            p = std::make_unique<std::vector<T>>();
        p->resize(size);
        auto const spn = span(*p);
        std::shared_ptr<std::vector<T>> shptr{
            p.release(),
            [self = this->shared_from_this()](std::vector<T> *pv) {
                if (not pv)
                    return;
                if constexpr (ClearRecycled)
                    pv->clear();
                {
                    std::scoped_lock lock(self->mutex);
                    self->recyclable.emplace_back(pv);
                }
                if constexpr (Blocking)
                    self->not_empty_condition.notify_one();
            }

        };
        return bucket<T>{spn, bucket_storage{shptr}};
    }

    /** \brief Implements sharable bucket source requirement. */
    [[nodiscard]] auto
    supports_shared_views() const noexcept -> bool override {
        return true;
    }

    /** \brief Implements sharable bucket source requirement. */
    [[nodiscard]] auto
    shared_view_of(bucket<T> const &bkt) -> bucket<T const> override {
        auto storage = bkt.template storage<bucket_storage>();
        return bucket<T const>{span<T const>(bkt), std::move(storage)};
    }
};

namespace internal {

template <typename Event, typename Downstream> class extract_bucket {
    static_assert(is_processor_v<Downstream,
                                 decltype(std::declval<Event>().data_bucket)>);

    Downstream downstream;

  public:
    explicit extract_bucket(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "extract_bucket");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void handle(Event const &event) { downstream.handle(event.data_bucket); }

    void handle(Event &&event) {
        downstream.handle(std::move(event).data_bucket);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * Create a processor that extracts the bucket carried by an event.
 *
 * \ingroup processors-io
 *
 * \tparam Event the event type, which must have the public data member
 * `bucket`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: pass its `bucket` field downstream
 * - Flush: pass through with no action
 */
template <typename Event, typename Downstream>
auto extract_bucket(Downstream &&downstream) {
    return internal::extract_bucket<Event, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
