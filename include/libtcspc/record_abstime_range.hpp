/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "context.hpp"
#include "event.hpp"
#include "introspect.hpp"
#include "numeric_traits.hpp"
#include "processor.hpp"

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace tcspc {

/**
 * \brief Access for `tcspc::record_abstime_range()` processor data.
 *
 * \ingroup context-access
 */
template <typename Abstime> class record_abstime_range_access {
    std::function<std::optional<Abstime>()> min_fn;
    std::function<std::optional<Abstime>()> max_fn;

  public:
    /** \private */
    template <typename FMin, typename FMax>
    explicit record_abstime_range_access(FMin min_func, FMax max_func)
        : min_fn(min_func), max_fn(max_func) {}

    /**
     * \brief Return the minimum abstime observed, or empty if no
     * abstime-stamped event was observed.
     */
    auto min() -> std::optional<Abstime> { return min_fn(); }

    /**
     * \brief Return the maximum abstime observed, or empty if no
     * abstime-stamped event was observed.
     */
    auto max() -> std::optional<Abstime> { return max_fn(); }
};

namespace internal {

template <typename NumericTraits, typename Downstream>
class record_abstime_range {
    using abstime_type = typename NumericTraits::abstime_type;

    std::optional<abstime_type> mn;
    std::optional<abstime_type> mx;

    Downstream downstream;

    // Cold data after downstream.
    access_tracker<record_abstime_range_access<abstime_type>> trk;

  public:
    explicit record_abstime_range(
        access_tracker<record_abstime_range_access<abstime_type>> &&tracker,
        Downstream downstream)
        : downstream(std::move(downstream)), trk(std::move(tracker)) {
        trk.register_access_factory([](auto &tracker) {
            auto *self = LIBTCSPC_OBJECT_FROM_TRACKER(record_abstime_range,
                                                      trk, tracker);
            return record_abstime_range_access<abstime_type>(
                [self] { return self->mn; }, [self] { return self->mx; });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "record_abstime_range");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E>
        requires handler_for<Downstream, std::remove_cvref_t<E>>
    void handle(E &&event) {
        if constexpr (abstime_stamped<std::remove_cvref_t<E>>) {
            static_assert(
                std::is_same_v<decltype(event.abstime), abstime_type>);
            if (not mn || event.abstime < *mn)
                mn = event.abstime;
            if (not mx || event.abstime > *mx)
                mx = event.abstime;
        }
        downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that records the range of abstime observed.
 *
 * \ingroup processors-stats
 *
 * The processor passes through all events. It examines events that have an
 * `abstime` field and tracks the minimum and maximum `abstime` observed. The
 * event's `abstime` field type must match `NumericTraits::abstime_type`.
 *
 * The minimum and maximum can be retrieved through a
 * `tcspc::record_abstime_range_access` retrieved from the `tcspc::context`
 * from which \p tracker was obtained. Each is empty (`std::nullopt`) until an
 * abstime-stamped event has been observed.
 *
 * \tparam NumericTraits numeric traits specifying `abstime_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param tracker access tracker for later access of the abstime range result
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - All types with `abstime` field: update running min/max; pass through
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename NumericTraits = default_numeric_traits, typename Downstream>
auto record_abstime_range(access_tracker<record_abstime_range_access<
                              typename NumericTraits::abstime_type>> &&tracker,
                          Downstream downstream) {
    return internal::record_abstime_range<NumericTraits, Downstream>(
        std::move(tracker), std::move(downstream));
}

} // namespace tcspc
