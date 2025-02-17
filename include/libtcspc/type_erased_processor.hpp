/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "core.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"
#include "type_list.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventList> class abstract_processor;

template <> class abstract_processor<type_list<>> {
  public:
    abstract_processor() = default;
    abstract_processor(abstract_processor const &) = delete;
    auto operator=(abstract_processor const &) = delete;
    abstract_processor(abstract_processor &&) = delete;
    auto operator=(abstract_processor &&) = delete;
    virtual ~abstract_processor() = default;
    [[nodiscard]] virtual auto introspect_node() const -> processor_info = 0;
    [[nodiscard]] virtual auto introspect_graph() const -> processor_graph = 0;
    virtual void flush() = 0;
};

template <typename Event0>
class abstract_processor<type_list<Event0>>
    : public abstract_processor<type_list<>> {
  public:
    virtual void handle(Event0 const &) = 0;
    virtual void handle(Event0 &&) = 0;
};

template <typename Event0, typename... Events>
class abstract_processor<type_list<Event0, Events...>>
    : public abstract_processor<type_list<Events...>> {
    using base_type = abstract_processor<type_list<Events...>>;

  public:
    using base_type::handle; // Import overload set
    virtual void handle(Event0 const &) = 0;
    virtual void handle(Event0 &&) = 0;
};

template <typename Interface, typename Proc, typename EventList>
class virtual_processor_impl;

template <typename Interface, typename Proc>
class virtual_processor_impl<Interface, Proc, type_list<>> : public Interface {
  protected:
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    Proc proc;

  public:
    explicit virtual_processor_impl(Proc proc) : proc(std::move(proc)) {}

    template <typename... Args>
    explicit virtual_processor_impl(Args &&...args)
        : proc(std::forward<Args>(args)...) {}

    [[nodiscard]] auto introspect_node() const -> processor_info final {
        return processor_info(this, "virtual_processor_impl");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph final {
        return proc.introspect_graph().push_entry_point(this);
    }

    void flush() final { proc.flush(); }
};

template <typename Interface, typename Proc, typename Event0,
          typename... Events>
class virtual_processor_impl<Interface, Proc, type_list<Event0, Events...>>
    : public virtual_processor_impl<Interface, Proc, type_list<Events...>> {
    using base_type =
        virtual_processor_impl<Interface, Proc, type_list<Events...>>;

  protected:
    using base_type::proc;

  public:
    using base_type::base_type;

    using base_type::handle; // Import overload set
    void handle(Event0 const &event) final { proc.handle(event); }
    void handle(Event0 &&event) final { proc.handle(std::move(event)); }
};

template <typename Proc, typename EventList>
using virtual_processor =
    virtual_processor_impl<abstract_processor<EventList>, Proc, EventList>;

} // namespace internal

/**
 * \brief Processor that type-erases the downstream processor.
 *
 * \ingroup processors-core
 *
 * \tparam EventList the event set handled by the processor
 *
 * \par Events handled
 * - Events in `EventList`: pass through with no action
 * - Flush: pass through with no action
 */
template <typename EventList> class type_erased_processor {
    using event_list = unique_type_list_t<EventList>;
    using abstract_processor = internal::abstract_processor<event_list>;

    template <typename Proc>
    using virtual_processor =
        typename internal::virtual_processor<Proc, event_list>;

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
     * The downstream processor must handle all of the events in \p EventList.
     *
     * \param downstream downstream processor
     */
    template <
        typename Downstream,
        typename = std::enable_if_t<
            not std::is_convertible_v<internal::remove_cvref_t<Downstream>,
                                      type_erased_processor> &&
            is_processor_of_list_v<internal::remove_cvref_t<Downstream>,
                                   event_list>>>
    explicit type_erased_processor(Downstream downstream)
        : proc(std::make_unique<virtual_processor<Downstream>>(
              std::move(downstream))) {}

    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "type_erased_processor");
    }

    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return proc->introspect_graph().push_entry_point(this);
    }

    /** \brief Implements processor requirement. */
    template <typename Event,
              typename = std::enable_if_t<is_convertible_to_type_list_member_v<
                  internal::remove_cvref_t<Event>, event_list>>>
    void handle(Event &&event) {
        proc->handle(std::forward<Event>(event));
    }

    /** \brief Implements processor requirement. */
    void flush() { proc->flush(); }
};

} // namespace tcspc
