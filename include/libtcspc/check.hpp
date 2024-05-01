/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <limits>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename DataTypes, bool RequireStrictlyIncreasing,
          typename Downstream>
class check_monotonic {
    static_assert(is_processor_v<Downstream, warning_event>);

    typename DataTypes::abstime_type last_seen =
        std::numeric_limits<typename DataTypes::abstime_type>::min();

    Downstream downstream;

    LIBTCSPC_NOINLINE void
    issue_warning(typename DataTypes::abstime_type abstime) {
        std::ostringstream stream;
        stream << "non-monotonic abstime: " << last_seen << " followed by "
               << abstime;
        downstream.handle(warning_event{stream.str()});
    }

  public:
    explicit check_monotonic(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "check_monotonic");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename Event, typename = std::enable_if_t<handles_event_v<
                                  Downstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        bool const monotonic = RequireStrictlyIncreasing
                                   ? event.abstime > last_seen
                                   : event.abstime >= last_seen;
        if (not monotonic)
            issue_warning(event.abstime);
        last_seen = event.abstime;
        downstream.handle(std::forward<Event>(event));
    }

    void handle(warning_event const &event) { downstream.handle(event); }

    void handle(warning_event &&event) { downstream.handle(std::move(event)); }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that checks that abstime is monotonically
 * increasing or nondecreasing.
 *
 * \ingroup processors-validation
 *
 * The processor passes through time-tagged events and checks that their
 * `abstime` is monotonic (that is, increasing or non-decreasing). If a
 * violation is detected, a `tcspc::warning_event` is emitted just before the
 * offending event.
 *
 * Checking abstime monotonicity is often a good way to detect gross issues in
 * the data, such as reading data in an incorrect format or using text mode to
 * read binary data.
 *
 * Any received `tcspc::warning_event` instances are passed through.
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 *
 * \tparam RequireStrictlyIncreasing if true, issue warning also on equal
 * `abstime`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - All types with `abstime` field: check monotonicity and emit
 *   `tcspc::warning_event` on violation; pass through
 * - `tcspc::warning_event`: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types,
          bool RequireStrictlyIncreasing = false, typename Downstream>
auto check_monotonic(Downstream &&downstream) {
    return internal::check_monotonic<DataTypes, RequireStrictlyIncreasing,
                                     Downstream>(
        std::forward<Downstream>(downstream));
}

namespace internal {

template <typename Event0, typename Event1, typename Downstream>
class check_alternating {
    static_assert(is_processor_v<Downstream, Event0, Event1, warning_event>);

    bool last_saw_0 = false;
    Downstream downstream;

    LIBTCSPC_NOINLINE void issue_warning() {
        downstream.handle(warning_event{"non-alternating events"});
    }

  public:
    explicit check_alternating(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "check_alternating");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E, typename = std::enable_if_t<
                              handles_event_v<Downstream, remove_cvref_t<E>>>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, Event0>) {
            if (last_saw_0)
                issue_warning();
            last_saw_0 = true;
        } else if constexpr (std::is_convertible_v<remove_cvref_t<E>,
                                                   Event1>) {
            if (not last_saw_0)
                issue_warning();
            last_saw_0 = false;
        }
        downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that checks that events of two types appear in
 * alternation.
 *
 * The processor passes through all events. It examines events of types \p
 * Event0 and \p Event1, and checks that they alternate, starting with \p
 * Event0. If a violation is detected, a `tcspc::warning_event` is emitted just
 * before the offending event.
 *
 * \ingroup processors-validation
 *
 * \tparam Event0 event type expected first
 *
 * \tparam Event1 event type expected second
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event0`, `Event1`: if not strictly alternating, starting with `Event0`,
 *   emit `tcspc::warning_event`; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Event0, typename Event1, typename Downstream>
auto check_alternating(Downstream &&downstream) {
    return internal::check_alternating<Event0, Event1, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
