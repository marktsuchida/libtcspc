/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <exception>
#include <limits>
#include <ostream>
#include <sstream>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename DataTraits, bool RequireStrictlyIncreasing,
          typename Downstream>
class check_monotonic {
    typename DataTraits::abstime_type last_seen =
        std::numeric_limits<typename DataTraits::abstime_type>::min();

    Downstream downstream;

    LIBTCSPC_NOINLINE void
    issue_warning(typename DataTraits::abstime_type abstime) {
        std::ostringstream stream;
        stream << "non-monotonic abstime: " << last_seen << " followed by "
               << abstime;
        downstream.handle(warning_event{stream.str()});
    }

  public:
    explicit check_monotonic(Downstream downstream)
        : downstream(std::move(downstream)) {}

    template <typename Event> void handle(Event const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTraits::abstime_type>);
        bool const monotonic = RequireStrictlyIncreasing
                                   ? event.abstime > last_seen
                                   : event.abstime >= last_seen;
        if (not monotonic)
            issue_warning(event.abstime);
        last_seen = event.abstime;
        downstream.handle(event);
    }

    void handle(warning_event const &event) { downstream.handle(event); }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that checks that abstime is monotonically
 * increasing or nondecreasing.
 *
 * \ingroup processors-timing
 *
 * This processor passes through time-tagged events and checks that their
 * abstime is monotonic (that is, increasing or not decreasing compared to the
 * previous event). If a violation is detected, a \c warning_event is emitted
 * just before the offending event.
 *
 * Checking abstime monotonicity is often a good way to detect gross issues in
 * the data, such as reading data in an incorrect format or using text mode to
 * read binary data.
 *
 * Any received \c warning_event instances are passed through.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam RequireStrictlyIncreasing if true, issue warning on consecutive
 * equal timestamps
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return check-monotonic processor
 */
template <typename DataTraits = default_data_traits,
          bool RequireStrictlyIncreasing = false, typename Downstream>
auto check_monotonic(Downstream &&downstream) {
    return internal::check_monotonic<DataTraits, RequireStrictlyIncreasing,
                                     Downstream>(
        std::forward<Downstream>(downstream));
}

namespace internal {

template <typename Event0, typename Event1, typename Downstream>
class check_alternating {
    bool last_saw_0 = false;
    Downstream downstream;

    LIBTCSPC_NOINLINE void issue_warning() {
        downstream.handle(warning_event{"non-alternating events"});
    }

  public:
    explicit check_alternating(Downstream downstream)
        : downstream(std::move(downstream)) {}

    void handle(Event0 const &event) {
        if (last_saw_0)
            issue_warning();
        last_saw_0 = true;
        downstream.handle(event);
    }

    void handle(Event1 const &event) {
        if (not last_saw_0)
            issue_warning();
        last_saw_0 = false;
        downstream.handle(event);
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that checks that events of two types appear in
 * alternation.
 *
 * The processor passes through all events. It examines events of types \c
 * Event0 and \c Event1, and checks that they alternate, starting with \c
 * Event0. If a violation is detected, a \c warning_event is emitted just
 * before the offending event.
 *
 * \ingroup processors-timing
 *
 * \tparam Event0 event type expected first
 *
 * \tparam Event1 event type expected second
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return check-alternating processor
 */
template <typename Event0, typename Event1, typename Downstream>
auto check_alternating(Downstream &&downstream) {
    return internal::check_alternating<Event0, Event1, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
