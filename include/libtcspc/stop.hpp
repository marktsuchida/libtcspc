/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "errors.hpp"
#include "event.hpp"
#include "introspect.hpp"
#include "processor.hpp"
#include "type_list.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventList, typename Exception, typename Downstream>
    requires processor<Downstream>
class stop {
    static_assert(type_list_like<EventList>);

    Downstream downstream;

    // Cold data after downstream.
    std::string message_prefix;

    template <typename Event>
    [[noreturn]] LIBTCSPC_NOINLINE void handle_stop(Event const &event) {
        if constexpr (std::is_same_v<Exception, end_of_processing>)
            downstream.flush();
        std::ostringstream stream;
        if constexpr (internal::ostreamable<Event>) {
            if (not message_prefix.empty())
                stream << message_prefix << ": ";
            stream << event;
        } else {
            stream << message_prefix;
        }
        throw Exception(stream.str());
    }

  public:
    explicit stop(std::string prefix, Downstream downstream)
        : downstream(std::move(downstream)),
          message_prefix(std::move(prefix)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "stop");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename Event>
        requires convertible_to_type_list_member<std::remove_cvref_t<Event>,
                                                 EventList>
    [[noreturn]] void handle(Event &&event) {
        handle_stop(event);
    }

    template <typename Event>
        requires(not convertible_to_type_list_member<
                     std::remove_cvref_t<Event>, EventList> and
                 handler_for<Downstream, std::remove_cvref_t<Event>>)
    void handle(Event &&event) {
        downstream.handle(std::forward<Event>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that ends the stream with an error when a given
 * event type is received.
 *
 * \ingroup processors-stopping
 *
 * If the triggering event type provides `operator<<` to `std::ostream`, the
 * formatted event is appended to the exception message after \p
 * message_prefix; otherwise the message consists of \p message_prefix only.
 *
 * \see `tcspc::stop()`
 *
 * \tparam EventList event types that should cause stream to end
 *
 * \tparam Exception exception type to use (must be constructible from
 * `std::string`)
 *
 * \tparam Downstream downstream processor type
 *
 * \param message_prefix error message prefix
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `EventList`: throw `Exception`
 * - Types not in `EventList`: pass through with no action
 * - Flush: pass through with no action
 */
template <typename EventList, typename Exception = std::runtime_error,
          typename Downstream>
auto stop_with_error(std::string message_prefix, Downstream downstream) {
    static_assert(not std::is_same_v<Exception, end_of_processing>);
    return internal::stop<EventList, Exception, Downstream>(
        std::move(message_prefix), std::move(downstream));
}

/**
 * \brief Create a processor that ends the stream when a given event type is
 * received.
 *
 * \ingroup processors-stopping
 *
 * If the triggering event type provides `operator<<` to `std::ostream`, the
 * formatted event is appended to the exception message after \p
 * message_prefix; otherwise the message consists of \p message_prefix only.
 *
 * \see `tcspc::stop_with_error()`
 *
 * \tparam EventList event types that should cause stream to end
 *
 * \tparam Downstream downstream processor type
 *
 * \param message_prefix error message prefix
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `EventList`: flush downstream and throw
 *   `tcspc::end_of_processing`
 * - Types not in `EventList`: pass through with no action
 * - Flush: pass through with no action
 */
template <typename EventList, typename Downstream>
auto stop(std::string message_prefix, Downstream downstream) {
    return internal::stop<EventList, end_of_processing, Downstream>(
        std::move(message_prefix), std::move(downstream));
}

} // namespace tcspc
