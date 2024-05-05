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
#include "histogram_policies.hpp"
#include "histogramming.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename OverflowPolicy, typename DataTypes, typename Downstream>
class histogram_elementwise {
    static_assert(is_any_of_v<OverflowPolicy, saturate_on_overflow_t,
                              error_on_overflow_t>);
    static_assert(is_processor_v<Downstream, histogram_event<DataTypes>,
                                 histogram_array_event<DataTypes>>);
    static_assert(not std::is_same_v<OverflowPolicy, saturate_on_overflow_t> ||
                  handles_event_v<Downstream, warning_event>);

    using internal_overflow_policy = std::conditional_t<
        std::is_same_v<OverflowPolicy, saturate_on_overflow_t>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    using bin_index_type = typename DataTypes::bin_index_type;
    using bin_type = typename DataTypes::bin_type;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    multi_histogram<bin_index_type, bin_type, internal_overflow_policy> mhist;
    bool saturated = false;
    null_journal<bin_index_type> journal; // Journaling not required
    Downstream downstream;

    LIBTCSPC_NOINLINE void handle_overflow() {
        if constexpr (std::is_same_v<OverflowPolicy, saturate_on_overflow_t>) {
            downstream.handle(warning_event{"histogram array saturated"});
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            error_on_overflow_t>) {
            throw histogram_overflow_error("histogram array bin overflowed");
        } else {
            static_assert(false_for_type<OverflowPolicy>::value);
        }
    }

  public:
    explicit histogram_elementwise(
        arg::num_elements<std::size_t> num_elements,
        arg::num_bins<std::size_t> num_bins,
        arg::max_per_bin<typename DataTypes::bin_type> max_per_bin,
        std::shared_ptr<bucket_source<typename DataTypes::bin_type>>
            buffer_provider,
        Downstream downstream)
        : bsource(std::move(buffer_provider)),
          mhist(hist_bucket, max_per_bin, num_bins, num_elements, true),
          downstream(std::move(downstream)) {
        if (num_elements.value == 0)
            throw std::invalid_argument(
                "histogram_elementsiwe must have at least 1 element");
        if (num_bins.value == 0)
            throw std::invalid_argument(
                "histogram_elementsiwe must have at least 1 bin per element");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "histogram_elementwise");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT>
    void handle(bin_increment_batch_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        if (hist_bucket.empty()) { // First time, or after completion.
            hist_bucket = bsource->bucket_of_size(mhist.num_elements() *
                                                  mhist.num_bins());
            mhist = decltype(mhist){hist_bucket, mhist, true};
        }
        assert(not mhist.is_complete());
        auto element_index = mhist.next_element_index();
        if (not mhist.apply_increment_batch(event.bin_indices, journal)) {
            if constexpr (std::is_same_v<OverflowPolicy,
                                         saturate_on_overflow_t>) {
                if (not saturated) {
                    saturated = true;
                    handle_overflow();
                }
            } else {
                return handle_overflow();
            }
        }

        auto const elem_event =
            histogram_event<DataTypes>{hist_bucket.subbucket(
                element_index * mhist.num_bins(), mhist.num_bins())};
        downstream.handle(elem_event);

        if (mhist.is_complete()) {
            downstream.handle(
                histogram_array_event<DataTypes>{std::move(hist_bucket)});
            hist_bucket = {};
            if constexpr (std::is_same_v<OverflowPolicy,
                                         saturate_on_overflow_t>) {
                saturated = false;
            }
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    template <typename DT> void handle(bin_increment_batch_event<DT> &&event) {
        handle(static_cast<bin_increment_batch_event<DT> const &>(event));
    }

    template <typename OtherEvent,
              typename = std::enable_if_t<
                  handles_event_v<Downstream, remove_cvref_t<OtherEvent>>>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collects time-divided arrays of histograms.
 *
 * \ingroup processors-histogramming
 *
 * The processor builds an array of histograms sequentially, computing each
 * element histogram from an incoming `tcspc::bin_increment_batch_event`. When
 * it has finished filling the histogram array, it starts over with a new
 * array. Each cycle of filling the histogram array is termed a _scan_. Each
 * scan consists of \p num_elements `tcspc::bin_increment_batch_event`s.
 *
 * The histogram array is stored in a `tcspc::bucket<DataTypes::bin_type>` of
 * size `num_elements * num_bins`. Each scan uses, sequentially, a new bucket
 * from the \p buffer_provider.
 *
 * On every bin increment batch received a `tcspc::histogram_event` is
 * emitted containing the corresponding subview of the histogram array bucket
 * (whose storage is observable but not extractable). At the end of each scan,
 * a `tcspc::histogram_array_event` is emitted, carrying the histogram array
 * bucket (storage can be extracted).
 *
 * \attention Behavior is undefined if an incoming
 * `tcspc::bin_increment_batch_event` contains a bin index beyond the size of
 * the histogram. The bin mapper should be chosen so that this does not occur.
 *
 * \tparam DataTypes data type set specifying `bin_index_type` and `bin_type`
 *
 * \tparam OverflowPolicy policy tag type (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param policy policy tag instance to select how to handle bin overflows
 * (`tcspc::saturate_on_overflow` or `tcspc::error_on_overflow`)
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
 * - `tcspc::bin_increment_batch_event<DT>`: apply the increments to the next
 *   element histogram of the array and emit (const)
 *   `tcspc::histogram_event<DataTypes>`; if the batch filled the last
 *   element of the array, emit (rvalue)
 *   `tcspc::histogram_array_event<DataTypes>`; if a bin overflowed, behavior
 *   (taken before emitting the above events) depends on `OverflowPolicy`:
 *   - If `tcspc::saturate_on_overflow_t`, ignore the increment, emitting
 *     `tcspc::warning_event` only on the first overflow of the scan
 *   - If `tcspc::error_on_overflow_t`, throw `tcspc::histogram_overflow_error`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename OverflowPolicy,
          typename Downstream>
auto histogram_elementwise(
    [[maybe_unused]] OverflowPolicy const &policy,
    arg::num_elements<std::size_t> num_elements,
    arg::num_bins<std::size_t> num_bins,
    arg::max_per_bin<typename DataTypes::bin_type> max_per_bin,
    std::shared_ptr<bucket_source<typename DataTypes::bin_type>>
        buffer_provider,
    Downstream &&downstream) {
    return internal::histogram_elementwise<OverflowPolicy, DataTypes,
                                           Downstream>(
        num_elements, num_bins, max_per_bin, std::move(buffer_provider),
        std::forward<Downstream>(downstream));
}

namespace internal {

template <typename ResetEvent, typename Policy, typename DataTypes,
          typename Downstream>
class histogram_elementwise_accumulate {
    static_assert(
        is_any_of_v<Policy, saturate_on_overflow_t, reset_on_overflow_t,
                    stop_on_overflow_t, error_on_overflow_t,
                    internal::error_on_overflow_and_skip_concluding_event_t>);
    using overflow_policy = std::conditional_t<
        std::is_same_v<
            Policy, internal::error_on_overflow_and_skip_concluding_event_t>,
        error_on_overflow_t, Policy>;

    // Do not require handling of concluding_histogram_array_event unless
    // reset_on_overflow.
    static_assert(is_processor_v<Downstream, histogram_event<DataTypes>,
                                 histogram_array_event<DataTypes>>);
    static_assert(
        not std::is_same_v<overflow_policy, saturate_on_overflow_t> ||
        handles_event_v<Downstream, warning_event>);
    static_assert(
        not std::is_same_v<overflow_policy, reset_on_overflow_t> ||
        handles_event_v<Downstream,
                        concluding_histogram_array_event<DataTypes>>);

    // Concluding event is not supported for saturate-on-overflow (no way to
    // roll back current scan). It is required for reset/stop-on-overflow
    // because it doesn't make much sense to use those policies without a
    // concluding event and I don't want to write unit tests for combinations
    // that are unlikely to be used.
    static constexpr bool need_concluding =
        not std::is_same_v<Policy, saturate_on_overflow_t> &&
        not std::is_same_v<
            Policy, internal::error_on_overflow_and_skip_concluding_event_t>;

    using internal_overflow_policy = std::conditional_t<
        std::is_same_v<overflow_policy, saturate_on_overflow_t>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    using bin_index_type = typename DataTypes::bin_index_type;
    using bin_type = typename DataTypes::bin_type;

    using journal_type =
        std::conditional_t<need_concluding,
                           bin_increment_batch_journal<bin_index_type>,
                           null_journal<bin_index_type>>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    multi_histogram_accumulation<bin_index_type, bin_type,
                                 internal_overflow_policy>
        mhista;
    bool saturated = false;
    journal_type journal;
    Downstream downstream;

    void lazy_start() {
        if (hist_bucket.empty()) { // First time, or after reset.
            hist_bucket = bsource->bucket_of_size(mhista.num_elements() *
                                                  mhista.num_bins());
            mhista = decltype(mhista){hist_bucket, mhista, true};
        }
    }

    void emit_concluding() {
        assert(mhista.is_consistent());
        downstream.handle(concluding_histogram_array_event<DataTypes>{
            std::move(hist_bucket)});
        hist_bucket = {};
    }

    [[noreturn]] void stop() {
        downstream.flush();
        throw end_of_processing("histogram array bin overflowed");
    }

    [[noreturn]] void overflow_error(char const *msg) {
        throw histogram_overflow_error(msg);
    }

    LIBTCSPC_NOINLINE void
    handle_overflow(bin_increment_batch_event<DataTypes> const &event) {
        if constexpr (std::is_same_v<overflow_policy,
                                     saturate_on_overflow_t>) {
            downstream.handle(warning_event{"histogram array saturated"});
        } else if constexpr (std::is_same_v<overflow_policy,
                                            reset_on_overflow_t>) {
            if (mhista.scan_index() == 0) {
                overflow_error(
                    "histogram array bin overflowed on a single batch");
            }
            mhista.roll_back_current_scan(journal);
            if constexpr (need_concluding)
                emit_concluding();
            hist_bucket = bsource->bucket_of_size(mhista.num_elements() *
                                                  mhista.num_bins());
            mhista = decltype(mhista){hist_bucket, mhista, true};
            mhista.replay(journal);
            return handle(event); // Recurse max once
        } else if constexpr (std::is_same_v<overflow_policy,
                                            stop_on_overflow_t>) {
            if constexpr (need_concluding) {
                mhista.roll_back_current_scan(journal);
                emit_concluding();
            }
            stop();
        } else if constexpr (std::is_same_v<overflow_policy,
                                            error_on_overflow_t>) {
            overflow_error("histogram array bin overflowed");
        } else {
            static_assert(false_for_type<overflow_policy>::value);
        }
    }

  public:
    explicit histogram_elementwise_accumulate(
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
                "histogram_elementsiwe_accumulate must have at least 1 element");
        if (num_bins.value == 0)
            throw std::invalid_argument(
                "histogram_elementsiwe_accumulate must have at least 1 bin per element");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "histogram_elementwise_accumulate");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT>
    void handle(bin_increment_batch_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        lazy_start();
        assert(not mhista.is_scan_complete());
        auto element_index = mhista.next_element_index();
        if (not mhista.apply_increment_batch(event.bin_indices, journal)) {
            if constexpr (std::is_same_v<overflow_policy,
                                         saturate_on_overflow_t>) {
                if (not saturated) {
                    saturated = true;
                    handle_overflow(event);
                }
            } else {
                return handle_overflow(event);
            }
        }

        auto const elem_event =
            histogram_event<DataTypes>{hist_bucket.subbucket(
                element_index * mhista.num_bins(), mhista.num_bins())};
        downstream.handle(elem_event);

        if (mhista.is_scan_complete()) {
            mhista.new_scan(journal);
            auto const array_event =
                histogram_array_event<DataTypes>{hist_bucket.subbucket(0)};
            downstream.handle(array_event);
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    template <typename DT> void handle(bin_increment_batch_event<DT> &&event) {
        handle(static_cast<bin_increment_batch_event<DT> const &>(event));
    }

    template <
        typename E,
        typename = std::enable_if_t<
            (std::is_convertible_v<remove_cvref_t<E>, ResetEvent> &&
             handles_event_v<Downstream,
                             concluding_histogram_array_event<DataTypes>>) ||
            (not std::is_convertible_v<remove_cvref_t<E>, ResetEvent> &&
             handles_event_v<Downstream, remove_cvref_t<E>>)>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, ResetEvent>) {
            if constexpr (need_concluding) {
                lazy_start();
                mhista.roll_back_current_scan(journal);
                emit_concluding();
            }
            hist_bucket = {};
            if constexpr (std::is_same_v<overflow_policy,
                                         saturate_on_overflow_t>) {
                saturated = false;
            }
            journal.clear();
        } else {
            downstream.handle(std::forward<E>(event));
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collects time-divided arrays of histograms,
 * accumulated over multiple scans.
 *
 * \ingroup processors-histogramming
 *
 * The processor builds an array of histograms sequentially, incrementing bins
 * in each element histogram based on incoming
 * `tcspc::bin_increment_batch_event`s. When it has finished updating all
 * elements of the histogram array (a _scan_), it returns to the beginning of
 * the array and continues to accumulate counts. A round of accumulation
 * (consisting of 0 or more scans) is ended when a \p ResetEvent is received,
 * upon which accumulation is restarted with an empty histogram array.
 *
 * The histogram array is stored in a `tcspc::bucket<DataTypes::bin_type>` of
 * size `num_elements * num_bins`. Each round of accumulation uses
 * (sequentially) a new bucket from the \p buffer_provider.
 *
 * On every bin increment batch received a `tcspc::histogram_event` is
 * emitted containing the corresponding subview of the histogram array bucket
 * (whose storage is observable but not extractable). At the end of each scan
 * through the array, a `tcspc::histogram_array_event` is emitted, containing a
 * view of the whole histogram array bucket (again, with observable but
 * non-extractable storage).
 *
 * At the end of each round of accumulation (i.e., upon a reset), increments
 * from any incomplete scan are rolled back and a
 * `tcspc::concluding_histogram_array_event` is emitted, carrying the histogram
 * array bucket (storage can be extracted).
 *
 * \attention Behavior is undefined if an incoming `tcspc::bin_increment_event`
 * contains a bin index beyond the size of the histogram. The bin mapper should
 * be chosen so that this does not occur.
 *
 * \tparam ResetEvent type of event causing histograms to reset
 *
 * \tparam DataTypes data type set specifying `bin_index_type` and `bin_type`
 *
 * \tparam Policy policy tag type (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param policy policy tag instance to select how to handle bin overflows
 * (`tcspc::saturate_on_overflow`, `tcspc::reset_on_overflow`,
 * `tcspc::stop_on_overflow`, or `tcspc::error_on_overflow`); for
 * `tcspc::error_on_overflow` only, can be combined (with `|` operator) with
 * `tcspc::skip_concluding_event`
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param buffer_provider bucket source providing series of buffers (a new one
 * is used after each reset)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bin_increment_batch_event<DT>`: apply the increments to the next
 *   element histogram of the array and emit (const)
 *   `tcspc::histogram_event<DataTypes>`; if the batch updated the
 *   last element of the array, emit (rvalue)
 *   `tcspc::histogram_array_event<DataTypes>`; if a bin overflowed, behavior
 *   (taken before emitting the above events) depends on the policy:
 *   - If `tcspc::saturate_on_overflow`, ignore the increment, emitting
 *     `tcspc::warning_event` only on the first overflow of the current round
 *     of accumulation
 *   - If `tcspc::reset_on_overflow`, behave as if a `ResetEvent` was received
 *     just prior to the current event; then replay any partial scan that was
 *     rolled back during the reset and reapply the current event (but throw
 *     `tcspc::histogram_overflow_error` if this causes an overflow by itself)
 *   - If `tcspc::stop_on_overflow`, behave as if a `ResetEvent` was received
 *     instead of the current event; then flush the downstream and throw
 *     `tcspc::end_of_processing`
 *   - If `tcspc::error_on_overflow`, throw `tcspc::histogram_overflow_error`
 * - `ResetEvent`: unless the policy is `tcspc::saturate_on_overflow` or
 *   includes `tcspc::skip_concluding_event`, emit (rvalue)
 *   `tcspc::concluding_histogram_array_event<DataTypes>` with the current
 *   histogram array (after rolling back any partial scan); then clear the
 *   array and other state
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename ResetEvent, typename DataTypes = default_data_types,
          typename Policy, typename Downstream>
auto histogram_elementwise_accumulate(
    [[maybe_unused]] Policy const &policy,
    arg::num_elements<std::size_t> num_elements,
    arg::num_bins<std::size_t> num_bins,
    arg::max_per_bin<typename DataTypes::bin_type> max_per_bin,
    std::shared_ptr<bucket_source<typename DataTypes::bin_type>>
        buffer_provider,
    Downstream &&downstream) {
    return internal::histogram_elementwise_accumulate<ResetEvent, Policy,
                                                      DataTypes, Downstream>(
        num_elements, num_bins, max_per_bin, std::move(buffer_provider),
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
