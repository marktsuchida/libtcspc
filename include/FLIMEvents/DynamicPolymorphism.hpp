/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ApplyClassTemplateToTupleElements.hpp"
#include "EventSet.hpp"

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

template <typename... Events> class virtual_processor;

template <> class virtual_processor<> {
  public:
    virtual ~virtual_processor() = default;
    virtual void handle_end(std::exception_ptr) noexcept = 0;
};

template <typename Event0>
class virtual_processor<Event0> : public virtual_processor<> {
  public:
    virtual void handle_event(Event0 const &) noexcept = 0;
};

template <typename Event0, typename Event1, typename... Events>
class virtual_processor<Event0, Event1, Events...>
    : public virtual_processor<Event1, Events...> {
    using BaseType = virtual_processor<Event1, Events...>;

  public:
    using BaseType::handle_event; // Import overload set
    virtual void handle_event(Event0 const &) noexcept = 0;
};

template <typename Interface, typename Proc, typename... Events>
class virtual_wrapped_processor_impl;

template <typename Interface, typename Proc>
class virtual_wrapped_processor_impl<Interface, Proc> : public Interface {
  protected:
    Proc proc;

  public:
    explicit virtual_wrapped_processor_impl(Proc &&proc)
        : proc(std::move(proc)) {}

    template <typename... Args>
    explicit virtual_wrapped_processor_impl(Args &&...args)
        : proc(std::forward<Args>(args)...) {}

    void handle_end(std::exception_ptr error) noexcept final {
        proc.handle_end(error);
    }

    Proc &wrapped() { return proc; }
};

template <typename Interface, typename Proc, typename Event0,
          typename... Events>
class virtual_wrapped_processor_impl<Interface, Proc, Event0, Events...>
    : public virtual_wrapped_processor_impl<Interface, Proc, Events...> {
    using BaseType =
        virtual_wrapped_processor_impl<Interface, Proc, Events...>;

  protected:
    using BaseType::proc;

  public:
    using BaseType::handle_event; // Import overload set
    void handle_event(Event0 const &event) noexcept final {
        proc.handle_event(event);
    }
};

template <typename Proc, typename... Events>
using virtual_wrapped_processor =
    virtual_wrapped_processor_impl<virtual_processor<Events...>, Proc,
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
 * \see virtual_wrapped_processor
 *
 * \tparam ESet the event set handled by implementations of this interface
 */
template <typename ESet>
using virtual_processor = internal::apply_class_template_to_tuple_elements_t<
    internal::virtual_processor, ESet>;

/**
 * \brief A dynamically polymorphic wrapper for a given processor type.
 *
 * A \c virtual_wrapped_processor with a given event set is derived from
 * \ref virtual_processor with the same event set, and has virtual \c
 * handle_event and \c handle_end functions.
 *
 * \tparam Proc the processor to wrap
 * \tparam ESet the event set handled by the processor
 */
template <typename Proc, typename ESet>
using virtual_wrapped_processor =
    internal::apply_class_template_to_tuple_elements_t<
        internal::virtual_wrapped_processor, ESet, Proc>;

/**
 * \brief Processor that invokes a dynamically polymorphic processor.
 *
 * This is a regular processor that contains a reference (\c shared_ptr) to a
 * dynamically polymorphic processor whose type can be determined at run time.
 *
 * \see virtual_processor
 *
 * \tparam ESet the event set handled by the processor
 */
template <typename ESet> class polymorphic_processor {
    std::shared_ptr<virtual_processor<ESet>> proc;

  public:
    /**
     * \brief Construct with the given dynamically polymorphic processor.
     *
     * \param proc the dynamically polymorphic processor that will handle
     * events and end-of-stream
     */
    polymorphic_processor(std::shared_ptr<virtual_processor<ESet>> proc)
        : proc(proc) {}

    // Rule of zero

    /** \brief Processor interface */
    template <typename E,
              typename = std::enable_if_t<contains_event_v<ESet, E>>>
    void handle_event(E const &event) noexcept {
        proc->handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        proc->handle_end(error);

        // No more calls will be made to proc, so avoid holding onto it
        proc.reset();
    }
};

} // namespace flimevt
