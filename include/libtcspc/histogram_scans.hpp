/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bucket.hpp"
#include "core.hpp"
#include "histogram_events.hpp"
#include "histogram_policies.hpp"
#include "histogramming.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <histogram_policy Policy, typename ResetEvent, typename DataTypes,
          typename Downstream>
class histogram_scans {
    static constexpr histogram_policy overflow_policy =
        Policy & histogram_policy::overflow_mask;
    static constexpr bool emit_concluding =
        (Policy & histogram_policy::emit_concluding_events) !=
        histogram_policy::default_policy;
    static constexpr bool reset_after_scan =
        (Policy & histogram_policy::reset_after_scan) !=
        histogram_policy::default_policy;
    static constexpr bool clear_every_scan =
        (Policy & histogram_policy::clear_every_scan) !=
        histogram_policy::default_policy;
    static constexpr bool clear_new_bucket =
        (Policy & histogram_policy::no_clear_new_bucket) ==
        histogram_policy::default_policy;

    static_assert(
        is_processor_v<Downstream, histogram_array_progress_event<DataTypes>,
                       histogram_array_event<DataTypes>>);
    static_assert(overflow_policy != histogram_policy::saturate_on_overflow ||
                  handles_event_v<Downstream, warning_event>);
    static_assert(
        (Policy & histogram_policy::emit_concluding_events) ==
            histogram_policy::default_policy ||
        handles_event_v<Downstream,
                        concluding_histogram_array_event<DataTypes>>);

    // There is no way to roll back a partial scan in saturate mode, so
    // concluding events cannot be emitted.
    static_assert(overflow_policy != histogram_policy::saturate_on_overflow ||
                  (Policy & histogram_policy::emit_concluding_events) ==
                      histogram_policy::default_policy);

    using internal_overflow_policy = std::conditional_t<
        overflow_policy == histogram_policy::saturate_on_overflow,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    using bin_index_type = typename DataTypes::bin_index_type;
    using bin_type = typename DataTypes::bin_type;

    using journal_type =
        std::conditional_t<emit_concluding ||
                               overflow_policy ==
                                   histogram_policy::reset_on_overflow,
                           bin_increment_batch_journal<bin_index_type>,
                           null_journal<bin_index_type>>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    multi_histogram_accumulation<bin_index_type, bin_type,
                                 internal_overflow_policy>
        mhista;
    bool saturate_warning_issued = false;
    journal_type journal;

    Downstream downstream;

    LIBTCSPC_NOINLINE void start_new_round() {
        auto const size = mhista.num_elements() * mhista.num_bins();
        hist_bucket = bsource->bucket_of_size(size);
        if constexpr (clear_new_bucket)
            std::fill(hist_bucket.begin(), hist_bucket.end(), bin_type{0});
        mhista = decltype(mhista)(hist_bucket, mhista, not clear_new_bucket);
    }

    void reset_without_replay() {
        if (hist_bucket.empty())
            start_new_round();
        if constexpr (emit_concluding) {
            mhista.roll_back_current_scan(journal);
            downstream.handle(concluding_histogram_array_event<DataTypes>{
                std::move(hist_bucket)});
        }
        hist_bucket = {};
        journal.clear();
        if constexpr (overflow_policy ==
                      histogram_policy::saturate_on_overflow)
            saturate_warning_issued = false;
    }

    void end_of_scan() {
        mhista.new_scan(journal, clear_every_scan);
        auto const array_event =
            histogram_array_event<DataTypes>{hist_bucket.subbucket(0)};
        downstream.handle(array_event);
        if constexpr (reset_after_scan)
            reset_without_replay();
    }

    LIBTCSPC_NOINLINE [[noreturn]] void overflow_error() {
        throw histogram_overflow_error("histogram array bin overflowed");
    }

    LIBTCSPC_NOINLINE [[noreturn]] void overflow_stop() {
        if constexpr (emit_concluding) {
            mhista.roll_back_current_scan(journal);
            downstream.handle(concluding_histogram_array_event<DataTypes>{
                std::move(hist_bucket)});
        }
        downstream.flush();
        throw end_of_processing("histogram array bin overflowed");
    }

    LIBTCSPC_NOINLINE void saturated_warning() {
        downstream.handle(warning_event{"histogram array bin saturated"});
        saturate_warning_issued = true;
    }

    template <typename DT>
    LIBTCSPC_NOINLINE void
    overflow_reset(bin_increment_batch_event<DT> const &event) {
        if (mhista.scan_index() == 0)
            throw histogram_overflow_error(
                "histogram array bin overflowed on first scan");
        mhista.roll_back_current_scan(journal);
        if constexpr (emit_concluding)
            downstream.handle(concluding_histogram_array_event<DataTypes>{
                std::move(hist_bucket)});
        start_new_round();
        mhista.replay(journal);
        // Recurse at most once, because overflow on first scan will be error.
        return handle(event);
    }

  public:
    explicit histogram_scans(
        arg::num_elements<std::size_t> num_elements,
        arg::num_bins<std::size_t> num_bins,
        arg::max_per_bin<typename DataTypes::bin_type> max_per_bin,
        std::shared_ptr<bucket_source<typename DataTypes::bin_type>>
            buffer_provider,
        Downstream downstream)
        : bsource(std::move(buffer_provider)),
          mhista(hist_bucket, max_per_bin, num_bins, num_elements, true),
          downstream(std::move(downstream)) {
        if (num_elements.value == 0)
            throw std::invalid_argument(
                "histogram_scans must have at least 1 element");
        if (num_bins.value == 0)
            throw std::invalid_argument(
                "histogram_scans must have at least 1 bin per element");
        if (max_per_bin.value < 0)
            throw std::invalid_argument(
                "histogram_scans max_per_bin must not be negative");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "histogram_scans");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT>
    void handle(bin_increment_batch_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        if (hist_bucket.empty())
            start_new_round();
        auto const element_index = mhista.next_element_index();
        if (not mhista.apply_increment_batch(event.bin_indices, journal)) {
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

        auto const progress = histogram_array_progress_event<DataTypes>{
            (element_index + 1) * mhista.num_bins(), hist_bucket.subbucket(0)};
        downstream.handle(progress);

        if (mhista.is_scan_complete())
            end_of_scan();
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    template <typename DT> void handle(bin_increment_batch_event<DT> &&event) {
        handle(static_cast<bin_increment_batch_event<DT> const &>(event));
    }

    template <typename E,
              typename = std::enable_if_t<
                  std::is_convertible_v<remove_cvref_t<E>, ResetEvent> ||
                  (not std::is_convertible_v<remove_cvref_t<E>, ResetEvent> &&
                   handles_event_v<Downstream, remove_cvref_t<E>>)>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, ResetEvent>)
            reset_without_replay();
        else
            downstream.handle(std::forward<E>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collects time-divided arrays of histograms
 * over repeated scans.
 *
 * \ingroup processors-histogramming
 *
 * The processor fills an array of histograms (held in a
 * `tcspc::bucket<DataTypes::bin_type>` provided by \p buffer_provider) by
 * sequentially visiting its elements (each a histogram) on each incoming
 * `tcspc::bin_increment_batch_event`. One such iteration of the array is
 * termed a _scan_. After a scan, the processor returns to the first element of
 * the array and continues to add increments (by default adding to the previous
 * scans).
 *
 * A _round_ consisting of multiple scans is ended by resetting, for example by
 * receiving a \p ResetEvent. After a reset, the histogram array is replaced
 * with a new bucket and a new round is started, in which handling of
 * subsequent bin increment batches begins at the first element of the array.
 *
 * The value of \p Policy can modify behavior (including disabling
 * the accumulating behavior) and specify what happens when a histogram bin
 * overflows; see `tcspc::histogram_policy` for details.
 *
 * The result is emitted in 3 ways:
 *
 * - A `tcspc::histogram_array_progress_event<DataTypes>` is emitted on each
 *   bin increment batch. It carries a view of the whole array and indicates
 *   how far the current scan has progressed.
 *
 * - A `tcspc::histogram_array_event<DataTypes>` is emitted as soon as each
 *   scan is finished, carrying a view of the histogram array.
 *
 * - If requested (i.e., \p Policy contains
 *   `tcspc::histogram_policy::emit_concluding_events`), a
 *   `tcspc::concluding_histogram_array_event<DataTypes>` is emitted upon each
 *   reset, but only after rolling back any incomplete scan, such that every
 *   element of the array contains counts from the same number of scans. This
 *   event carries a bucket with extractable storage.
 *
 * \attention Behavior is undefined if an incoming
 * `tcspc::bin_increment_batch_event` contains a bin index beyond the size of
 * the histogram. The bin maper should be chosen so that this does not occur.
 *
 * \tparam Policy policy specifying behavior of the processor
 *
 * \tparam ResetEvent type of event causing a new round to start
 *
 * \tparam DataTypes data type set specifying `bin_index_type` and `bin_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param buffer_provider bucket source providing series of buffers for each
 * histogram array
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bin_increment_batch_event<DT>`: apply the
 *   increments to the next element histogram of the array (after clearing it
 *   if this is the first scan of a round or `Policy` has
 *   `tcspc::histogram_policy::clear_every_scan` set) and, if there is no bin
 *   overflow, emit (const) `tcspc::histogram_array_progress_event<DataTypes>`;
 *   if the batch was for the last element of the array (end of a scan), also
 *   emit (const) `tcspc::histogram_array_event<DataTypes>` and if, in
 *   addition, `Policy` has `tcspc::histogram_policy::reset_after_scan` set,
 *   perform a reset as if a `ResetEvent` was received. If a bin overflowed,
 *   behavior depends on the value of `Policy &
 *   tcspc::histogram_policy::overflow_mask`:
 *   - If `tcspc::histogram_policy::error_on_overflow` (the default), throw
 *     `tcspc::histogram_overflow_error`
 *   - If `tcspc::histogram_policy::stop_on_overflow`, roll back the current
 *     scan and emit `tcspc::concluding_histogram_array_event<DataTypes>` if
 *     `Policy` has `tcspc::histogram_policy::emit_concluding_events` set, then
 *     flush the downstream and throw `tcspc::end_of_processing`
 *   - If `tcspc::histogram_policy::saturate_on_overflow`, emit a
 *     `tcspc::warning_event` (but only if this is the first overflow in the
 *     current round); then ignore all increments that would result in
 *     overflow, but do apply non-overflowing increments and proceed to emit
 *     `tcspc::histogram_array_progress_event<DataTypes>`
 *   - If `tcspc::histogram_policy::reset_on_overflow`, perform a reset, as if
 *     a `ResetEvent` was received just prior to the current event; then, in
 *     the fresh array, replay any partial scan that was rolled back during the
 *     reset, and reapply the current event (but throw
 *     `tcspc::histogram_overflow_error` if the overflow occurred during the
 *     first scan of the round, so that there is no infinite recursion)
 * - `ResetEvent`: if `Policy` has
 *   `tcspc::histogram_policy::emit_concluding_events` set, roll back any
 *   partial scan and emit (rvalue)
 *   `tcspc::concluding_histogram_array_event<DataTypes>`; then start a new
 *   round by arranging to switch to a new bucket for the histogram array on
 *   the next `tcspc::bin_increment_batch_event`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <histogram_policy Policy = histogram_policy::default_policy,
          typename ResetEvent = never_event,
          typename DataTypes = default_data_types, typename Downstream>
auto histogram_scans(
    arg::num_elements<std::size_t> num_elements,
    arg::num_bins<std::size_t> num_bins,
    arg::max_per_bin<typename DataTypes::bin_type> max_per_bin,
    std::shared_ptr<bucket_source<typename DataTypes::bin_type>>
        buffer_provider,
    Downstream &&downstream) {
    return internal::histogram_scans<Policy, ResetEvent, DataTypes,
                                     Downstream>(
        num_elements, num_bins, max_per_bin, std::move(buffer_provider),
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
