/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "apply_class_template.hpp"
#include "event_set.hpp"

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

template <typename... Events> class abstract_processor_impl;

template <> class abstract_processor_impl<> {
  public:
    abstract_processor_impl() = default;
    abstract_processor_impl(abstract_processor_impl const &) = delete;
    auto operator=(abstract_processor_impl const &) = delete;
    abstract_processor_impl(abstract_processor_impl &&) = delete;
    auto operator=(abstract_processor_impl &&) = delete;
    virtual ~abstract_processor_impl() = default;
    virtual void handle_end(std::exception_ptr const &) noexcept = 0;
};

template <typename Event0>
class abstract_processor_impl<Event0> : public abstract_processor_impl<> {
  public:
    virtual void handle_event(Event0 const &) noexcept = 0;
};

template <typename Event0, typename Event1, typename... Events>
class abstract_processor_impl<Event0, Event1, Events...>
    : public abstract_processor_impl<Event1, Events...> {
    using base_type = abstract_processor_impl<Event1, Events...>;

  public:
    using base_type::handle_event; // Import overload set
    virtual void handle_event(Event0 const &) noexcept = 0;
};

template <typename Interface, typename Proc, typename... Events>
class virtual_processor_impl;

template <typename Interface, typename Proc>
class virtual_processor_impl<Interface, Proc> : public Interface {
  protected:
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    Proc proc;

  public:
    explicit virtual_processor_impl(Proc &&proc) : proc(std::move(proc)) {}

    template <typename... Args>
    explicit virtual_processor_impl(Args &&...args)
        : proc(std::forward<Args>(args)...) {}

    void handle_end(std::exception_ptr const &error) noexcept final {
        proc.handle_end(error);
    }

    auto wrapped() -> Proc & { return proc; }
};

template <typename Interface, typename Proc, typename Event0,
          typename... Events>
class virtual_processor_impl<Interface, Proc, Event0, Events...>
    : public virtual_processor_impl<Interface, Proc, Events...> {
    using base_type = virtual_processor_impl<Interface, Proc, Events...>;

  protected:
    using base_type::proc;

  public:
    using base_type::base_type;

    using base_type::handle_event; // Import overload set
    void handle_event(Event0 const &event) noexcept final {
        proc.handle_event(event);
    }
};

template <typename Proc, typename... Events>
using virtual_processor =
    virtual_processor_impl<abstract_processor_impl<Events...>, Proc,
                           Events...>;

} // namespace internal

/**
 * \brief Abstract base class defining interface for dynamically polymorphic
 * processors.
 *
 * Dynamically polymorphic processors have virtual \c handle_event and \c
 * handle_end functions.
 *
 * \see polymorphic_processor
 * \see virtual_processor
 *
 * \tparam Es the event set handled by implementations of this interface
 */
template <typename Es>
using abstract_processor =
    internal::apply_class_template_t<internal::abstract_processor_impl, Es>;

/**
 * \brief A dynamically polymorphic wrapper for a given processor type.
 *
 * A \c virtual_processor with a given event set is derived from
 * \ref abstract_processor with the same event set, and has virtual \c
 * handle_event and \c handle_end functions.
 *
 * \tparam Proc the processor to wrap
 * \tparam Es the event set handled by the processor
 */
template <typename Proc, typename Es>
using virtual_processor =
    internal::apply_class_template_t<internal::virtual_processor, Es, Proc>;

/**
 * \brief Processor that invokes a dynamically polymorphic processor.
 *
 * This is a regular processor that contains a reference (\c unique_ptr) to a
 * dynamically polymorphic processor whose type can be determined at run time.
 *
 * \see abstract_processor
 *
 * \tparam Es the event set handled by the processor
 */
template <typename Es> class polymorphic_processor {
    std::unique_ptr<abstract_processor<Es>> proc;

  public:
    /**
     * \brief Construct with the given dynamically polymorphic processor.
     *
     * \param proc the dynamically polymorphic processor that will handle
     * events and end-of-stream
     */
    polymorphic_processor(std::unique_ptr<abstract_processor<Es>> proc)
        : proc(std::move(proc)) {}

    // Rule of zero

    /** \brief Processor interface */
    template <typename E, typename = std::enable_if_t<contains_event_v<Es, E>>>
    void handle_event(E const &event) noexcept {
        proc->handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr const &error) noexcept {
        proc->handle_end(error);

        // No more calls will be made to proc, so avoid holding onto it
        proc.reset();
    }
};

} // namespace flimevt
