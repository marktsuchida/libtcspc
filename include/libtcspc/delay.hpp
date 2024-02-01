/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"

#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename DataTraits, typename Downstream> class delay {
    typename DataTraits::abstime_type delta;
    Downstream downstream;

  public:
    explicit delay(typename DataTraits::abstime_type delta,
                   Downstream downstream)
        : delta(delta), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "delay");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename TimeTaggedEvent>
    void handle(TimeTaggedEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTraits::abstime_type>);
        TimeTaggedEvent copy(event);
        copy.abstime += delta;
        downstream.handle(copy);
    }

    void flush() { downstream.flush(); }
};

template <typename DataTraits, typename Downstream> class zero_base_abstime {
    bool initialized = false;
    typename DataTraits::abstime_type minus_delta{};
    Downstream downstream;

  public:
    explicit zero_base_abstime(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "zero_base_abstime");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename TimeTaggedEvent>
    void handle(TimeTaggedEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTraits::abstime_type>);
        if (not initialized) {
            minus_delta = event.abstime;
            initialized = true;
        }
        TimeTaggedEvent copy(event);
        // Support integer wrap-around by using unsigned type for subtraction.
        copy.abstime = static_cast<decltype(copy.abstime)>(
            as_unsigned(event.abstime) - as_unsigned(minus_delta));
        downstream.handle(copy);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that applies an abstime offset to all events.
 *
 * \ingroup processors-timing
 *
 * All events processed must have an \c abstime field, and no other fields
 * derived from the abstime (because only the \c abstime field will be
 * adjusted).
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param delta abstime offset to apply (can be negative)
 *
 * \param downstream downstream processor
 *
 * \return delay processor
 *
 * \inevents
 * \event{All events, passed through with time delay applied}
 * \endevents
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto delay(typename DataTraits::abstime_type delta, Downstream &&downstream) {
    return internal::delay<DataTraits, Downstream>(
        delta, std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that offsets abstime so that the first event is at
 * time zero.
 *
 * \ingroup processors-timing
 *
 * This can be used to ensure that downstream processing will not encounter
 * integer overflow within a moderate amount of time. Even if the \c
 * abstime_type is a signed integer type, wrap-around is handled correctly.
 *
 * \see delay
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return zero_base_abstime processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto zero_base_abstime(Downstream &&downstream) {
    return internal::zero_base_abstime<DataTraits, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
