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

class context;

/**
 * \brief Tracker that mediates access to objects via a `tcspc::context`.
 *
 * \ingroup context
 *
 * This is a movable (noncopyable) object that tracks its address when moved.
 * The address is stored in the associated `tcspc::context` from
 * which the tracker was created.
 *
 * An object stores the tracker instance as a data member and also registers an
 * access factory in its constructor. This allows code to later access the
 * object state, even after the tracked object has been embedded in an outer
 * object (as with a processor incorporated into a processing graph).
 *
 * \tparam Access type of access interface for the tracked object
 */
template <typename Access> class access_tracker {
    std::shared_ptr<context> ctx; // Null iff 'empty' state.
    std::string name;
    std::function<Access(access_tracker &)> access_factory;

    friend class context;

    // Construct only via friend context.
    explicit access_tracker(std::shared_ptr<context> context, std::string name)
        : ctx(std::move(context)), name(std::move(name)) {}

  public:
    /**
     * \brief Construct an empty tracker.
     *
     * Empty instances cannot be used other than by assigning a non-empty
     * instance.
     *
     * Non-empty instances can only be obtained from a
     * `tcspc::context`.
     */
    access_tracker() = default;

    /** \cond hidden-from-docs */
    access_tracker(access_tracker const &) = delete;
    auto operator=(access_tracker const &) = delete;

    // Definitions for move and destruction are below, out of line.
    access_tracker(access_tracker &&other) noexcept;
    auto operator=(access_tracker &&rhs) noexcept -> access_tracker &;
    ~access_tracker();
    /** \endcond */

    /**
     * \brief Register an access factory with this tracker's context.
     *
     * This is usually called in the tracked object's constructor to arrange
     * for later access to the object via its corresponding access type.
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
 * \brief Context for enabling access to objects after they have been
 * incorporated into a processing graph.
 *
 * \ingroup context
 *
 * Instances are nonmovable and must be handled by `std::shared_ptr`.
 *
 * A context mediates external access to the state of individual
 * objects (typically processors) within the processing graph. This is done by
 * means of a `tcspc::access_tracker` object that is obtained from the context
 * and embedded into the object. The tracker moves and is destroyed together
 * with the tracked object, allowing the context to track how to access the
 * object after being moved into the processing graph.
 *
 * A name is associated with each tracked object. The name must be unique
 * within a given context (and may not be reused even after destroying the
 * corresponding tracker).
 *
 * Actual access to object state is through an _access_ object whose type
 * is defined by the object and whose instances can be obtained from the
 * context by name.
 */
class context : public std::enable_shared_from_this<context> {
    // Map keys are names identifying the tracked object and values are pointer
    // to access_tracker<Access> for some Access. Values are kept up to date
    // when a tracker is moved or destroyed. If the tracked object has been
    // destroyed, the entry remains and the value is an empty std::any()).
    // Reuse of name is not allowed.
    std::unordered_map<std::string, std::any> trackers;

    template <typename Access> friend class access_tracker;

    template <typename Access>
    void update_tracker_address(std::string const &name,
                                access_tracker<Access> *ptr) {
        // Enforce no type change in std::any; just update the value directly.
        *std::any_cast<access_tracker<Access> *>(&trackers.at(name)) = ptr;
    }

    context() = default;

  public:
    // Move not permitted (only handled by shared_ptr).
    /** \cond hidden-from-docs */
    context(context const &) = delete;
    auto operator=(context const &) = delete;
    context(context &&) = delete;
    auto operator=(context &&) = delete;
    ~context() = default;
    /** \endcond */

    /**
     * \brief Create an instance.
     */
    static auto create() -> std::shared_ptr<context> {
        return std::shared_ptr<context>(new context());
    }

    /**
     * \brief Obtain a tracker for an object with the given name.
     *
     * \tparam Access the object's access type
     *
     * \param name name to assign to the tracked object
     *
     * \return tracker to be stored in the object
     */
    template <typename Access>
    auto tracker(std::string name) -> access_tracker<Access> {
        if (trackers.count(name) != 0) {
            throw std::logic_error(
                "cannot create tracker for existing name: " + name);
        }
        auto ret = access_tracker<Access>(shared_from_this(), std::move(name));
        trackers.insert({ret.name, std::any(&ret)});
        return ret;
    }

    /**
     * \brief Obtain an access for the named object.
     *
     * \attention The returned access object becomes invalid if the object to
     * which it refers is moved or destroyed. Do not store access instances.
     *
     * \tparam Access the access type
     *
     * \param name the name identifying the tracked object
     */
    template <typename Access> auto access(std::string const &name) -> Access {
        auto tracker_ptr =
            std::any_cast<access_tracker<Access> *>(trackers.at(name));
        if (tracker_ptr == nullptr)
            throw std::logic_error("cannot access destroyed object: " + name);
        return tracker_ptr->access_factory(*tracker_ptr);
    }
};

/** \cond hidden-from-docs */

// Move/destroy for access_tracker, defined out-of-line because they require
// the definition of context.

template <typename Access>
access_tracker<Access>::access_tracker(access_tracker &&other) noexcept
    : ctx(std::move(other.ctx)), name(std::move(other.name)),
      access_factory(std::move(other.access_factory)) {
    if (ctx)
        ctx->update_tracker_address(name, this);
}

template <typename Access>
auto access_tracker<Access>::operator=(access_tracker &&rhs) noexcept
    -> access_tracker & {
    ctx = std::move(rhs.ctx);
    name = std::move(rhs.name);
    access_factory = std::move(rhs.access_factory);
    if (ctx)
        ctx->update_tracker_address(name, this);
    return *this;
}

template <typename Access> access_tracker<Access>::~access_tracker() {
    if (ctx)
        ctx->update_tracker_address<Access>(name, nullptr);
}

/** \endcond */

// NOLINTBEGIN

// Locally silence GCC/Clang warnings for using offsetof() on
// non-standard-layout types. Semicolons used only to assist clang-format.
#if defined(__GNUC__) || defined(__clang__)
#define LIBTCSPC_INTERNAL_DISABLE_OFFSETOF_WARNING                            \
    _Pragma("GCC diagnostic push");                                           \
    _Pragma("GCC diagnostic ignored \"-Winvalid-offsetof\"")
#define LIBTCSPC_INTERNAL_POP_OFFSETOF_WARNING _Pragma("GCC diagnostic pop")
#else
#define LIBTCSPC_INTERNAL_DISABLE_OFFSETOF_WARNING
#define LIBTCSPC_INTERNAL_POP_OFFSETOF_WARNING
#endif

/**
 * \brief Recover the object address from a `tcspc::access_tracker` embedded in
 * the object.
 *
 * \ingroup context
 *
 * This can be used in the implementation of an access factory (see
 * `tcspc::access_tracker::register_access_factory()`).
 *
 * \hideinitializer
 *
 * \param obj_type object type (no commas or angle brackets)
 *
 * \param tracker_field_name name of data member of \p obj_type holding the
 * tracker
 *
 * \param tracker the tracker (must be lvalue)
 *
 * \return pointer to the object
 */
#define LIBTCSPC_OBJECT_FROM_TRACKER(obj_type, tracker_field_name, tracker)   \
    std::invoke([&tracker]() {                                                \
        LIBTCSPC_INTERNAL_DISABLE_OFFSETOF_WARNING;                           \
        return reinterpret_cast<std::add_pointer_t<obj_type>>(                \
            reinterpret_cast<std::byte *>(&(tracker)) -                       \
            offsetof(obj_type, tracker_field_name));                          \
        LIBTCSPC_INTERNAL_POP_OFFSETOF_WARNING;                               \
    })

// Note: offsetof() is "conditionally-supported" on non-standard-layout types
// as of C++17. I expect this not to be a problem in practice, as long as
// virtual inheritance is not involved.

// Pointer arithmetic on the underlying bytes of an object is technically
// undefined behavior in C++ (but not C), because no array of bytes was created
// there. But compilers treat this as valid (see https://wg21.link/p1839r6).
// (Also see https://wg21.link/p3407r0 on another technical aspect.)

// NOLINTEND

} // namespace tcspc
