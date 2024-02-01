/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "introspect.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventSet, typename Exception, typename Downstream>
class stop {
    Downstream downstream;

    // Cold data after downstream.
    std::string message_prefix;

    template <typename Event>
    [[noreturn]] LIBTCSPC_NOINLINE void handle_stop(Event const &event) {
        if constexpr (std::is_same_v<Exception, end_processing>)
            downstream.flush();
        std::ostringstream stream;
        if (not message_prefix.empty())
            stream << message_prefix << ": ";
        stream << event;
        throw Exception(stream.str());
    }

  public:
    explicit stop(std::string prefix, Downstream downstream)
        : downstream(std::move(downstream)),
          message_prefix(std::move(prefix)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "stop");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename Event> void handle(Event const &event) {
        if constexpr (contains_event_v<EventSet, Event>)
            handle_stop(event);
        else
            downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that ends the stream with an error when a given
 * event type is received.
 *
 * \ingroup processors-basic
 *
 * \see stop
 *
 * \tparam EventSet event types that should cause stream to end
 *
 * \tparam Exception exception type to use (must be constructible from
 * std::string)
 *
 * \tparam Downstream downstream processor type
 *
 * \param message_prefix error message prefix
 *
 * \param downstream downstream processor
 */
template <typename EventSet, typename Exception = std::runtime_error,
          typename Downstream>
auto stop_with_error(std::string message_prefix, Downstream &&downstream) {
    static_assert(not std::is_same_v<Exception, end_processing>);
    return internal::stop<EventSet, Exception, Downstream>(
        std::move(message_prefix), std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that ends the stream when a given event type is
 * received.
 *
 * \ingroup processors-basic
 *
 * \see stop_with_error
 *
 * \tparam EventSet event types that should cause stream to end
 *
 * \tparam Downstream downstream processor type
 *
 * \param message_prefix error message prefix
 *
 * \param downstream downstream processor
 */
template <typename EventSet, typename Downstream>
auto stop(std::string message_prefix, Downstream &&downstream) {
    return internal::stop<EventSet, end_processing, Downstream>(
        std::move(message_prefix), std::forward<Downstream>(downstream));
}

} // namespace tcspc
