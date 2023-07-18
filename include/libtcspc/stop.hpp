/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"

#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EventSet, typename Exception, typename Downstream>
class stop_with_error {
    bool stopped = false;

    Downstream downstream;

    // Cold data after downstream.
    std::string message_prefix;

  public:
    explicit stop_with_error(std::string message, Downstream &&downstream)
        : downstream(std::move(downstream)),
          message_prefix(std::move(message)) {}

    template <typename Event> void handle_event(Event const &event) noexcept {
        if (stopped)
            return;
        if constexpr (contains_event_v<EventSet, Event>) {
            stopped = true;
            if (message_prefix.empty()) {
                downstream.handle_end({});
            } else {
                std::ostringstream stream;
                stream << message_prefix << ": " << event;
                downstream.handle_end(
                    std::make_exception_ptr(Exception(stream.str())));
            }
        } else {
            downstream.handle_event(event);
        }
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        if (not stopped)
            downstream.handle_end(error);
    }
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
 * \param message error message ("error" is used if empty)
 *
 * \param downstream downstream processor
 */
template <typename EventSet, typename Exception = std::runtime_error,
          typename Downstream>
auto stop_with_error(std::string message, Downstream &&downstream) {
    return internal::stop_with_error<EventSet, Exception, Downstream>(
        message.empty() ? "error" : std::move(message),
        std::forward<Downstream>(downstream));
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
 * \param downstream downstream processor
 */
template <typename EventSet, typename Downstream>
auto stop(Downstream &&downstream) {
    return internal::stop_with_error<EventSet, std::runtime_error, Downstream>(
        {}, std::forward<Downstream>(downstream));
}

} // namespace tcspc
