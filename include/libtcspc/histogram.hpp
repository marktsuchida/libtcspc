/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bucket.hpp"
#include "common.hpp"
#include "core.hpp"
#include "data_types.hpp"
#include "errors.hpp"
#include "histogram_events.hpp"
#include "histogram_policy.hpp"
#include "histogramming.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <histogram_policy Policy, typename ResetEvent, typename DataTypes,
          typename Downstream>
class histogram {
    static constexpr histogram_policy overflow_policy =
        Policy & histogram_policy::overflow_mask;
    static constexpr bool emit_concluding =
        (Policy & histogram_policy::emit_concluding_events) !=
        histogram_policy::default_policy;

    static_assert(is_processor_v<Downstream, histogram_event<DataTypes>>);
    static_assert(overflow_policy != histogram_policy::saturate_on_overflow ||
                  handles_event_v<Downstream, warning_event>);
    static_assert(
        (Policy & histogram_policy::emit_concluding_events) ==
            histogram_policy::default_policy ||
        handles_event_v<Downstream, concluding_histogram_event<DataTypes>>);

    using internal_overflow_policy = std::conditional_t<
        overflow_policy == histogram_policy::saturate_on_overflow,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    using bin_index_type = typename DataTypes::bin_index_type;
    using bin_type = typename DataTypes::bin_type;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    single_histogram<bin_index_type, bin_type, internal_overflow_policy> shist;
    bool saturate_warning_issued = false;

    Downstream downstream;

    LIBTCSPC_NOINLINE void start_new_round() {
        hist_bucket = bsource->bucket_of_size(shist.num_bins());
        shist = decltype(shist){hist_bucket, shist};
        shist.clear();
    }

    void reset() {
        if (hist_bucket.empty())
            start_new_round();
        if constexpr (emit_concluding) {
            downstream.handle(
                concluding_histogram_event<DataTypes>{std::move(hist_bucket)});
        }
        hist_bucket = {};
        if constexpr (overflow_policy ==
                      histogram_policy::saturate_on_overflow)
            saturate_warning_issued = false;
    }

    LIBTCSPC_NOINLINE [[noreturn]] void overflow_error() {
        throw histogram_overflow_error("histogram bin overflowed");
    }

    LIBTCSPC_NOINLINE [[noreturn]] void overflow_stop() {
        reset();
        downstream.flush();
        throw end_of_processing("histogram bin overflowed");
    }

    LIBTCSPC_NOINLINE void saturated_warning() {
        downstream.handle(warning_event{"histogram bin saturated"});
        saturate_warning_issued = true;
    }

    template <typename DT>
    LIBTCSPC_NOINLINE void
    overflow_reset(bin_increment_event<DT> const &event) {
        if (shist.max_per_bin() == 0)
            overflow_error();
        reset();
        // Recurse at most once, because overflow on single increment (when
        // max_per_bin == 0) is error.
        return handle(event);
    }

  public:
    explicit histogram(
        arg::num_bins<std::size_t> num_bins,
        arg::max_per_bin<bin_type> max_per_bin,
        std::shared_ptr<bucket_source<bin_type>> buffer_provider,
        Downstream downstream)
        : bsource(std::move(buffer_provider)),
          shist(hist_bucket, max_per_bin, num_bins),
          downstream(std::move(downstream)) {
        if (num_bins.value == 0)
            throw std::invalid_argument("histogram must have at least 1 bin");
        if (max_per_bin.value < 0)
            throw std::invalid_argument(
                "histogram max_per_bin must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "histogram");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT> void handle(bin_increment_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        if (hist_bucket.empty())
            start_new_round();
        if (not shist.apply_increments({&event.bin_index, 1})) {
            if constexpr (overflow_policy ==
                          histogram_policy::error_on_overflow) {
                overflow_error(); // noreturn
            } else if constexpr (overflow_policy ==
                                 histogram_policy::stop_on_overflow) {
                overflow_stop(); // noreturn
            } else if constexpr (overflow_policy ==
                                 histogram_policy::saturate_on_overflow) {
                if (not saturate_warning_issued)
                    saturated_warning();
            } else if constexpr (overflow_policy ==
                                 histogram_policy::reset_on_overflow) {
                return overflow_reset(event);
            }
        }

        auto const hist_event =
            histogram_event<DataTypes>{hist_bucket.subbucket(0)};
        downstream.handle(hist_event);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    template <typename DT> void handle(bin_increment_event<DT> &&event) {
        handle(static_cast<bin_increment_event<DT> const &>(event));
    }

    template <typename E,
              typename = std::enable_if_t<
                  std::is_convertible_v<remove_cvref_t<E>, ResetEvent> ||
                  (not std::is_convertible_v<remove_cvref_t<E>, ResetEvent> &&
                   handles_event_v<Downstream, remove_cvref_t<E>>)>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, ResetEvent>)
            reset();
        else
            downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collects a histogram.
 *
 * \ingroup processors-histogramming
 *
 * The processor fills a histogram (held in a
 * `tcspc::bucket<DataTypes::bin_type>` provided by \p buffer_provider) by
 * incrementing the bin given by each incoming `tcspc::bin_increment_event`.
 *
 * A _round_ of accumulation is ended by resetting, for example by receiving a
 * \p ResetEvent. After a reset, the histogram is replaced with a new bucket
 * and a new round is started.
 *
 * The value of \p Policy can modify behavior and specify what happens when a
 * histogram bin overflows; see `tcspc::histogram_policy` for details.
 *
 * The result is emitted in 2 ways:
 *
 * - A `tcspc::histogram_event<DataTypes>` is emitted on each bin increment,
 *   carrying a view of the histogram.
 *
 * - If requested (i.e., \p Policy contains
 *   `tcspc::histogram_policy::emit_concluding_events`), a
 *   `tcspc::concluding_histogram_event<DataTypes>` is emitted upon each reset.
 *   This event carries a bucket with extractable storage.
 *
 * \attention Behavior is undefined if an incoming `tcspc::bin_increment_event`
 * contains a bin index beyond the size of the histogram. The bin mapper should
 * be chosen so that this does not occur.
 *
 * \tparam Policy policy specifying behavior of the processor
 *
 * \tparam ResetEvent type of event causing a new round to start
 *
 * \tparam DataTypes data type set specifying `bin_index_type` and `bin_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param num_bins number of bins in the histogram (must match the bin mapper
 * used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param buffer_provider bucket source providing series of buffers (a new
 * bucket is used after every reset)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 *
 * - `tcspc::bin_increment_event<DT>`: apply the increment to the histogram
 * and, if there is no bin overflow, emit (const)
 *   `tcspc::histogram_event<DataTypes>`. If a bin overflowed,
 *   behavior depends on the value of `Policy &
 *   tcspc::histogram_policy::overflow_mask`:
 *   - If `tcspc::histogram_policy::error_on_overflow` (the default), throw
 *     `tcspc::histogram_overflow_error`
 *   - If `tcspc::histogram_policy::stop_on_overflow`, emit
 *     `tcspc::concluding_histogram_event<DataTypes>` if `Policy` has
 *     `tcspc::histogram_policy::emit_concluding_events` set, then flush the
 *     downstream and throw `tcspc::end_of_processing`
 *   - If `tcspc::histogram_policy::saturate_on_overflow`, emit a
 *     `tcspc::warning_event` (but only if this is the first overflow in the
 *     current round); then ignore the increment but do proceed to emit
 *     `tcspc::histogram_event<DataTypes>`
 *   - If `tcspc::histogram_policy::reset_on_overflow`, perform a reset, as if
 *     a `ResetEvent` was received just prior to the current event; then, in
 *     the fresh histogram, reapply the current event (but throw
 *     `tcspc::histogram_overflow_error` if `max_per_bin` equals 0)
 * - `ResetEvent`: if `Policy` has
 *   `tcspc::histogram_policy::emit_concluding_events` set, emit (rvalue)
 *   `tcspc::concluding_histogram_event<DataTypes>` with the current
 *   histogram; then start a new round by arranging to switch to a new bucket
 *   for the histogram on the next `tcspc::bin_increment_event`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <histogram_policy Policy = histogram_policy::default_policy,
          typename ResetEvent = never_event,
          typename DataTypes = default_data_types, typename Downstream>
auto histogram(arg::num_bins<std::size_t> num_bins,
               arg::max_per_bin<typename DataTypes::bin_type> max_per_bin,
               std::shared_ptr<bucket_source<typename DataTypes::bin_type>>
                   buffer_provider,
               Downstream &&downstream) {
    return internal::histogram<Policy, ResetEvent, DataTypes, Downstream>(
        num_bins, max_per_bin, std::move(buffer_provider),
        std ::forward<Downstream>(downstream));
}

} // namespace tcspc
