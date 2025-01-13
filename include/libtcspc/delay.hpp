/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "data_types.hpp"
#include "int_arith.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename DataTypes, typename Downstream> class delay {
    typename DataTypes::abstime_type delta;
    Downstream downstream;

  public:
    explicit delay(arg::delta<typename DataTypes::abstime_type> delta,
                   Downstream downstream)
        : delta(delta.value), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "delay");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    // Note: We could support rvalue events and edit the abstime in place. But
    // in practice events are expected to be small (so copy overhead is
    // probably minor) and may originate from a buffer (in which case we do not
    // want to rely on the compiler to optimize out the write to the event in a
    // heap buffer). Also only handling lvalues is simpler.

    template <typename TimeTaggedEvent,
              typename = std::enable_if_t<handles_event_v<
                  Downstream, remove_cvref_t<TimeTaggedEvent>>>>
    void handle(TimeTaggedEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        TimeTaggedEvent copy(event);
        copy.abstime = add_with_wrap(copy.abstime, delta);
        downstream.handle(std::move(copy));
    }

    void flush() { downstream.flush(); }
};

template <typename DataTypes, typename Downstream> class zero_base_abstime {
    bool initialized = false;
    typename DataTypes::abstime_type minus_delta{};
    Downstream downstream;

  public:
    explicit zero_base_abstime(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "zero_base_abstime");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    // Handle only const lvalue (see note on delay::handle()).

    template <typename TimeTaggedEvent,
              typename = std::enable_if_t<handles_event_v<
                  Downstream, remove_cvref_t<TimeTaggedEvent>>>>
    void handle(TimeTaggedEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        if (not initialized) {
            minus_delta = event.abstime;
            initialized = true;
        }
        TimeTaggedEvent copy(event);
        copy.abstime = subtract_with_wrap(event.abstime, minus_delta);
        downstream.handle(std::move(copy));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that applies an abstime offset to all events.
 *
 * \ingroup processors-timeline
 *
 * All events processed must have an `abstime` field, and no other fields
 * derived from the abstime (because only the `abstime` field will be
 * adjusted). Even if the `abstime_type` is a signed integer type, wrap-around
 * is handled correctly.
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 *
 * \tparam Downstream downstream processor type
 *
 * \param delta abstime offset to apply (can be negative)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - All types with `abstime` field: pass through with \p delta added to
 *   `abstime`
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto delay(arg::delta<typename DataTypes::abstime_type> delta,
           Downstream downstream) {
    return internal::delay<DataTypes, Downstream>(delta,
                                                  std::move(downstream));
}

/**
 * \brief Create a processor that offsets abstime so that the first event is at
 * time zero.
 *
 * \ingroup processors-timeline
 *
 * This can be used to ensure that downstream processing will not encounter
 * integer overflow within a moderate amount of time. Even if the
 * `abstime_type` is a signed integer type, wrap-around is handled correctly.
 *
 * \see delay
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - All types with `abstime` field: pass through with the `abstime` made
 *   relative to the first event encountered
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto zero_base_abstime(Downstream downstream) {
    return internal::zero_base_abstime<DataTypes, Downstream>(
        std::move(downstream));
}

} // namespace tcspc
