/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "apply_class_template.hpp"
#include "common.hpp"
#include "event_set.hpp"
#include "introspect.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace tcspc {

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
    [[nodiscard]] virtual auto introspect_node() const -> processor_info = 0;
    [[nodiscard]] virtual auto introspect_graph() const -> processor_graph = 0;
    virtual void flush() = 0;
};

template <typename Event0>
class abstract_processor_impl<Event0> : public abstract_processor_impl<> {
  public:
    virtual void handle(Event0 const &) = 0;
};

template <typename Event0, typename Event1, typename... Events>
class abstract_processor_impl<Event0, Event1, Events...>
    : public abstract_processor_impl<Event1, Events...> {
    using base_type = abstract_processor_impl<Event1, Events...>;

  public:
    using base_type::handle; // Import overload set
    virtual void handle(Event0 const &) = 0;
};

template <typename Interface, typename Proc, typename... Events>
class virtual_processor_impl;

template <typename Interface, typename Proc>
class virtual_processor_impl<Interface, Proc> : public Interface {
  protected:
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    Proc proc;

  public:
    explicit virtual_processor_impl(Proc proc) : proc(std::move(proc)) {}

    template <typename... Args>
    explicit virtual_processor_impl(Args &&...args)
        : proc(std::forward<Args>(args)...) {}

    [[nodiscard]] auto introspect_node() const -> processor_info final {
        processor_info info(this, "virtual_processor_impl");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph final {
        auto g = proc.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void flush() final { proc.flush(); }

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

    using base_type::handle; // Import overload set
    void handle(Event0 const &event) final { proc.handle(event); }
};

template <typename Proc, typename... Events>
using virtual_processor =
    virtual_processor_impl<abstract_processor_impl<Events...>, Proc,
                           Events...>;

} // namespace internal

/**
 * \brief Processor that type-erases the downstream processor.
 *
 * \ingroup processors-basic
 *
 * \tparam EventSet the event set handled by the processor
 */
template <typename EventSet> class type_erased_processor {
    using abstract_processor =
        internal::apply_class_template_t<internal::abstract_processor_impl,
                                         EventSet>;

    template <typename Proc>
    using virtual_processor =
        internal::apply_class_template_t<internal::virtual_processor, EventSet,
                                         Proc>;

    std::unique_ptr<abstract_processor> proc;

  public:
    /**
     * \brief Construct with a stub downstream processor.
     *
     * The stub processor discards all events.
     */
    type_erased_processor()
        : proc(std::make_unique<virtual_processor<null_sink>>()) {}

    /**
     * \brief Construct with the given downstream processor.
     *
     * The downstream processor must handle all of the events in \c EventSet.
     *
     * \param downstream downstream processor
     */
    template <
        typename Downstream,
        typename = std::enable_if_t<handles_event_set_v<Downstream, EventSet>>>
    explicit type_erased_processor(Downstream &&downstream)
        : proc(std::make_unique<virtual_processor<Downstream>>(
              std::forward<Downstream>(downstream))) {}

    /** \brief Processor interface */
    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "type_erased_processor");
        return info;
    }

    /** \brief Processor interface */
    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = proc->introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    /** \brief Processor interface */
    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void handle(Event const &event) {
        proc->handle(event);
    }

    /** \brief Processor interface */
    void flush() { proc->flush(); }
};

} // namespace tcspc
