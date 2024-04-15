/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <any>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace tcspc {

class processor_context;

/**
 * \brief Tracker object that mediates access to processors via a
 * `tcspc::processor_context`.
 *
 * \ingroup processor-context
 *
 * This is a movable (noncopyable) object that tracks its address when moved.
 * The address is stored in the associated `tcspc::processor_context` from
 * which the tracker was created.
 *
 * A processor stores the instance as a data member and also registers an
 * access factory in its constructor. This allows code other than upstream
 * processors to later access the processor state.
 *
 * \tparam Access type of access interface for processor
 */
template <typename Access> class processor_tracker {
    std::shared_ptr<processor_context> ctx; // Null iff 'empty' state.
    std::string name;
    std::function<Access(processor_tracker &)> access_factory;

    friend class processor_context;

    // Construct only via friend processor_context.
    explicit processor_tracker(std::shared_ptr<processor_context> context,
                               std::string name)
        : ctx(std::move(context)), name(std::move(name)) {}

  public:
    /**
     * \brief Construct an empty tracker.
     *
     * Empty instances cannot be used other than by assigning a non-empty
     * instance.
     *
     * Non-empty instances can only be obtained from a
     * `tcspc::processor_context`.
     */
    processor_tracker() = default;

    /** \cond hidden-from-docs */
    processor_tracker(processor_tracker const &) = delete;
    auto operator=(processor_tracker const &) = delete;

    // Definitions for move and destruction are below, out of line.
    processor_tracker(processor_tracker &&other) noexcept;
    auto operator=(processor_tracker &&rhs) noexcept -> processor_tracker &;
    ~processor_tracker();
    /** \endcond */

    /**
     * \brief Register an access factory with this tracker's context.
     *
     * This is usually called in the processor's constructor to arrange for
     * later access to the processor via an access.
     *
     * \tparam F factory function type
     *
     * \param factory function taking reference to this tracker and returning
     * the access object of type \p Access
     */
    template <typename F> void register_access_factory(F factory) {
        assert(ctx);
        assert(not access_factory);
        access_factory = std::move(factory);
    }
};

/**
 * \brief Context for enabling access to processors after they have been
 * incorporated into a processing graph.
 *
 * \ingroup processor-context
 *
 * Instances are nonmovable must be handled by `std::shared_ptr`.
 *
 * A processor context mediates access to the state of individual processors
 * from outside of the processing graph. This is done by means of
 * `tcspc::processor_tracker` objects that are obtained from the context and
 * need to be embedded into processors when constructing the latter. The
 * tracker moves and is destroyed together with the processor, allowing the
 * context to track how to access the processor after being moved into the
 * processing graph.
 *
 * Each processor/tracker is associated a name, which must be unique within a
 * given context (and may not be reused even after destroying the corresponding
 * processor/tracker).
 *
 * Actual access to processor state is through an _access_ object whose type
 * is defined by the processor and whose instances can be obtained from the
 * context by giving the processor name.
 */
class processor_context
    : public std::enable_shared_from_this<processor_context> {
    // Map keys are processor names and values are pointer to
    // processor_tracker<Access> for some Access. Values are kept up to
    // date when tracker is moved or destroyed. If the tracked processor has
    // been destroyed, the entry remains and the value is an empty std::any()).
    // Reuse of name is not allowed.
    std::unordered_map<std::string, std::any> trackers;

    template <typename Access> friend class processor_tracker;

    template <typename Access>
    void update_tracker_address(std::string const &processor_name,
                                processor_tracker<Access> *ptr) {
        // Enforce no type change in std::any; just update the value directly.
        *std::any_cast<processor_tracker<Access> *>(
            &trackers.at(processor_name)) = ptr;
    }

    processor_context() = default;

  public:
    // Move not permitted (only handled by shared_ptr).
    /** \cond hidden-from-docs */
    processor_context(processor_context const &) = delete;
    auto operator=(processor_context const &) = delete;
    processor_context(processor_context &&) = delete;
    auto operator=(processor_context &&) = delete;
    ~processor_context() = default;
    /** \endcond */

    /**
     * \brief Create an instance.
     */
    static auto create() -> std::shared_ptr<processor_context> {
        return std::shared_ptr<processor_context>(new processor_context());
    }

    /**
     * \brief Obtain a tracker for a processor with the given name.
     *
     * \tparam Access the processor's access type
     *
     * \param processor_name name to assign to the tracked processor
     *
     * \return tracker to be stored in the processor
     */
    template <typename Access>
    auto tracker(std::string processor_name) -> processor_tracker<Access> {
        if (trackers.count(processor_name) != 0) {
            throw std::logic_error(
                "cannot create tracker for existing processor name: " +
                processor_name);
        }
        auto ret = processor_tracker<Access>(shared_from_this(),
                                             std::move(processor_name));
        trackers.insert({ret.name, std::any(&ret)});
        return ret;
    }

    /**
     * \brief Obtain an access for the named processor.
     *
     * \attention The returned access object becomes invalid if the processor
     * to which it refers is moved or destroyed. Do not store access instances.
     *
     * \tparam Access the access type (documented by the processor)
     *
     * \param processor_name the processor name
     */
    template <typename Access>
    auto access(std::string const &processor_name) -> Access {
        auto tracker_ptr = std::any_cast<processor_tracker<Access> *>(
            trackers.at(processor_name));
        if (tracker_ptr == nullptr)
            throw std::logic_error("cannot access destroyed processor: " +
                                   processor_name);
        return tracker_ptr->access_factory(*tracker_ptr);
    }
};

/** \cond hidden-from-docs */

// Move/destroy for processor_tracker, defined out-of-line because they require
// the definition of processor_context.

template <typename Access>
processor_tracker<Access>::processor_tracker(
    processor_tracker &&other) noexcept
    : ctx(std::move(other.ctx)), name(std::move(other.name)),
      access_factory(std::move(other.access_factory)) {
    if (ctx)
        ctx->update_tracker_address(name, this);
}

template <typename Access>
auto processor_tracker<Access>::operator=(processor_tracker &&rhs) noexcept
    -> processor_tracker & {
    ctx = std::move(rhs.ctx);
    name = std::move(rhs.name);
    access_factory = std::move(rhs.access_factory);
    if (ctx)
        ctx->update_tracker_address(name, this);
    return *this;
}

template <typename Access> processor_tracker<Access>::~processor_tracker() {
    if (ctx)
        ctx->update_tracker_address<Access>(name, nullptr);
}

/** \endcond */

// NOLINTBEGIN

/**
 * \brief Recover the processor address from a `tcspc::processor_tracker`
 * embedded in the processor object.
 *
 * \ingroup processor-context
 *
 * This can be used in the implementation of a processor access factory (see
 * `tcspc::processor_tracker::register_access_factory()`).
 *
 * \hideinitializer
 *
 * \param proc_type processor type (no commas or angle brackets)
 *
 * \param tracker_field_name name of data member of \p proc_type holding the
 * tracker
 *
 * \param tracker the tracker (must be lvalue)
 *
 * \return pointer to the processor
 */
#define LIBTCSPC_PROCESSOR_FROM_TRACKER(proc_type, tracker_field_name,        \
                                        tracker)                              \
    reinterpret_cast<std::add_pointer_t<proc_type>>(                          \
        reinterpret_cast<std::byte *>(&(tracker)) -                           \
        offsetof(proc_type, tracker_field_name))

// Note: offsetof() on non-standard-layout types is "conditionally-supported"
// as of C++17 for non-standard-layout types. I expect this not to be a problem
// in practice, as long as virtual inheritance is not involved.

// Pointer arithmetic on the underlying bytes of an object is technically
// undefined behavior in C++ (but not C), because no array of bytes was created
// there. But compilers treat this as valid (see https://wg21.link/p1839r5).

// NOLINTEND

} // namespace tcspc
