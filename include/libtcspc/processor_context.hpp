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
 * \brief Tracker object that mediates access to processors via a \c
 * processor_context.
 *
 * \ingroup misc
 *
 * Instances should be obtained from a \ref processor_context. A processor
 * stores the instance as a data member and also registers an access factory
 * in its constructor. This allows code other than upstream processors to later
 * access the processor state.
 *
 * \see processor_context
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
     * \brief Default constructor.
     *
     * Constructs in the "empty" state, unconnected to a processor context.
     * Empty instances cannot be used other than by assigning a non-empty
     * instance.
     */
    processor_tracker() = default;

    // Noncopyable.
    processor_tracker(processor_tracker const &) = delete;
    auto operator=(processor_tracker const &) = delete;

    /** \brief Move constructor. */
    processor_tracker(processor_tracker &&other) noexcept;

    /** \brief Move assignment operator. */
    auto operator=(processor_tracker &&rhs) noexcept -> processor_tracker &;

    /** \brief Destructor. */
    ~processor_tracker();

    /**
     * \brief Register an access factory with this tracker's context.
     *
     * This is usually called in the processors constructor to arrange for
     * later access to the processor via an access.
     *
     * \tparam F factory function type
     *
     * \param factory factory function taking reference to this tracker and
     * returning the access of type \c Access
     */
    template <typename F> void register_access_factory(F factory) {
        assert(ctx);
        assert(not access_factory);
        access_factory = std::move(factory);
    }
};

/**
 * \brief Context for enabling access to processors after they have been
 * incorporated into a processing chain.
 *
 * \ingroup misc
 *
 * Instances must be handled by \c std::shared_ptr.
 *
 * A processor context mediates access to the state of individual processors
 * from outside of the processing chain/pipeline. This is done by means of \c
 * processor_tracker objects that are obtained from the context and need to be
 * embedded into processors when constructing the latter. The tracker moves and
 * is destroyed together with the processor, allowing the context to track how
 * to access the processor after being moved into the processing chain.
 *
 * Each processor/tracker is associated a name, which must be unique within a
 * given context (and may not be reused even after destroying the corresponding
 * processor/tracker).
 *
 * Actual access to processor state is through an \e access object whose type
 * is defined by the processor and whose instances can be obtained from the
 * context by giving the processor name.
 *
 * \see processor_tracker
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

  public:
    processor_context() = default;
    processor_context(processor_context const &) = delete;
    auto operator=(processor_context const &) = delete;
    processor_context(processor_context &&) = delete;
    auto operator=(processor_context &&) = delete;
    ~processor_context() = default;

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
     * The returned access is only valid while the processor is alive and
     * unmoved, so it should not be stored by the caller.
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

// NOLINTBEGIN

/**
 * \brief Recover the processor address from an embedded processor tracker.
 *
 * \ingroup misc
 *
 * This can be used in the implementation of a processor access factory (see
 * \c processor_tracker::register_access_factory).
 *
 * \param proc_type processor type (no commas or angle brackets)
 *
 * \param tracker_field_name name of data member of proc_type holding the
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
