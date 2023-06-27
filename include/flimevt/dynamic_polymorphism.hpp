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
 * \brief Processor that type-erases the downstream processor.
 *
 * \tparam Es the event set handled by the processor
 */
template <typename Es> class polymorphic_processor {
    using abstract_processor =
        internal::apply_class_template_t<internal::abstract_processor_impl,
                                         Es>;

    template <typename Proc>
    using virtual_processor =
        internal::apply_class_template_t<internal::virtual_processor, Es,
                                         Proc>;

    std::unique_ptr<abstract_processor> proc;

  public:
    /**
     * \brief Construct with the given downstream processor.
     *
     * The downstream processor must handle all of the events in \c Es.
     *
     * \param downstream downstream processor
     */
    template <typename D,
              typename = std::enable_if_t<handles_event_set_v<D, Es>>>
    explicit polymorphic_processor(D &&downstream)
        : proc(std::make_unique<virtual_processor<D>>(
              std::forward<D>(downstream))) {}

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
