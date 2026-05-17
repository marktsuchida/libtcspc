/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
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
                           warning_event const &rhs) noexcept
        -> bool = default;

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream, warning_event const &event)
        -> std::ostream & {
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

namespace internal {

class sink_all {
  public:
    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "sink_all");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    template <typename Event> void handle(Event const & /* event */) {}

    void flush() {}
};

template <typename Downstream> class source_nothing {
    bool flushed = false;
    Downstream downstream;

  public:
    explicit source_nothing(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "source_nothing");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void flush() {
        if (flushed) {
            throw std::logic_error(
                "source_nothing may not be flushed a second time");
        }
        flushed = true;
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that sinks any event and the end-of-stream and
 * does nothing.
 *
 * \ingroup processors-core
 *
 * \return processor
 *
 * \par Events handled
 * - All types: ignore
 * - Flush: ignore
 */
inline auto sink_all() { return internal::sink_all(); }

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
template <typename Downstream> auto source_nothing(Downstream downstream) {
    return internal::source_nothing<Downstream>(std::move(downstream));
}

} // namespace tcspc
