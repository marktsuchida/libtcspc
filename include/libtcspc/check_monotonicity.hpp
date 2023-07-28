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
#include <utility>

namespace tcspc {

namespace internal {

template <typename DataTraits, bool RequireStrictlyIncreasing,
          typename Downstream>
class check_monotonicity {
    typename DataTraits::abstime_type last_seen =
        std::numeric_limits<typename DataTraits::abstime_type>::min();

    Downstream downstream;

  public:
    explicit check_monotonicity(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    template <typename Event> void handle_event(Event const &event) noexcept {
        bool const monotonic = RequireStrictlyIncreasing
                                   ? event.abstime > last_seen
                                   : event.abstime >= last_seen;
        if (not monotonic) {
            std::ostringstream stream;
            stream << "non-monotonic abstime: " << last_seen << " followed by "
                   << event.abstime;
            downstream.handle_event(warning_event{stream.str()});
        }
        last_seen = event.abstime;
        downstream.handle_event(event);
    }

    void handle_event(warning_event const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
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
 * \return check-monotonicity processor
 */
template <typename DataTraits = default_data_traits,
          bool RequireStrictlyIncreasing = false, typename Downstream>
auto check_monotonicity(Downstream &&downstream) {
    return internal::check_monotonicity<DataTraits, RequireStrictlyIncreasing,
                                        Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
