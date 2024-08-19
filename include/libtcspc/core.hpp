/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "introspect.hpp"

#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace tcspc {

/**
 * \brief An event type indicating a warning.
 *
 * \ingroup events-core
 *
 * Processors that encounter recoverable errors emit this event. It can be used
 * together with `tcspc::stop()` or `tcspc::stop_with_error()` to stop
 * processing.
 *
 * Processors that generate this event should also pass through this event. In
 * this way, multiple warning-emitting processors can be chained before a
 * single point where the warnings are handled.
 */
struct warning_event {
    /** \brief A human-readable message describing the warning. */
    std::string message;

    /** \brief Equality comparison operator. */
    friend auto operator==(warning_event const &lhs,
                           warning_event const &rhs) noexcept -> bool {
        return lhs.message == rhs.message;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(warning_event const &lhs,
                           warning_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           warning_event const &event) -> std::ostream & {
        return stream << event.message;
    }
};

/**
 * \brief An event type whose instances never occur.
 *
 * \ingroup events-core
 *
 * This can be used to configure unused inputs to processors.
 */
struct never_event {
    never_event() = delete;
};

/**
 * \brief Processor that sinks any event and the end-of-stream and does
 * nothing.
 *
 * \ingroup processors-core
 *
 * \par Events handled
 * - All types: ignore
 * - Flush: ignore
 */
class null_sink {
  public:
    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "null_sink");
    }

    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    /** \brief Implements processor requirement. */
    template <typename Event> void handle(Event const & /* event */) {}

    /** \brief Implements processor requirement. */
    void flush() {}
};

namespace internal {

template <typename Downstream> class null_source {
    bool flushed = false;
    Downstream downstream;

  public:
    explicit null_source(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "null_source");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void flush() {
        if (flushed) {
            throw std::logic_error(
                "null_source may not be flushed a second time");
        }
        flushed = true;
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that sources an empty stream.
 *
 * \ingroup processors-core
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Flush: pass through with no action
 */
template <typename Downstream> auto null_source(Downstream &&downstream) {
    return internal::null_source<Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
