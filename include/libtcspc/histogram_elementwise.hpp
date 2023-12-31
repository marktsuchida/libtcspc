/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "binning.hpp"
#include "common.hpp"
#include "histogram_events.hpp"
#include "histogramming.hpp"
#include "span.hpp"

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename OverflowStrategy, typename DataTraits, typename Downstream>
class histogram_elementwise {
  public:
    using bin_index_type = typename DataTraits::bin_index_type;
    using bin_type = typename DataTraits::bin_type;
    static_assert(is_any_of_v<OverflowStrategy, saturate_on_overflow,
                              error_on_overflow>);

  private:
    using internal_overflow_strategy = std::conditional_t<
        std::is_same_v<OverflowStrategy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    // We use unique_ptr<T[]> rather than vector<T> because, among other
    // things, the latter cannot be allocated without zeroing.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    std::unique_ptr<bin_type[]> hist_arr_mem;
    span<bin_type> hist_arr;
    multi_histogram<bin_index_type, bin_type, internal_overflow_strategy>
        mhist;
    histogram_stats stats;
    null_journal<bin_index_type> journal; // Journaling not required
    abstime_range<typename DataTraits::abstime_type> cycle_time_range;
    Downstream downstream;

    [[noreturn]] void overflow_error() {
        throw histogram_overflow_error("elementwise histogram bin overflowed");
    }

    [[noreturn]] void handle_overflow() {
        if constexpr (std::is_same_v<OverflowStrategy, saturate_on_overflow>) {
            unreachable();
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            error_on_overflow>) {
            overflow_error();
        } else {
            static_assert(false_for_type<OverflowStrategy>::value);
        }
    }

  public:
    explicit histogram_elementwise(std::size_t num_elements,
                                   std::size_t num_bins, bin_type max_per_bin,
                                   Downstream downstream)
        : hist_arr_mem(new bin_type[num_elements * num_bins]),
          hist_arr(hist_arr_mem.get(), num_elements * num_bins),
          mhist(hist_arr, max_per_bin, num_bins, num_elements, true),
          downstream(std::move(downstream)) {}

    template <typename DT>
    void handle(bin_increment_batch_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTraits::bin_index_type>);
        assert(not mhist.is_complete());
        auto element_index = mhist.next_element_index();
        if (not mhist.apply_increment_batch(event.bin_indices, stats, journal))
            handle_overflow();
        cycle_time_range.extend(event.time_range);

        downstream.handle(element_histogram_event<DataTraits>{
            event.time_range, element_index,
            autocopy_span<bin_type>(mhist.element_span(element_index)), stats,
            0});

        if (mhist.is_complete()) {
            downstream.handle(histogram_array_event<DataTraits>{
                cycle_time_range, autocopy_span<bin_type>(hist_arr), stats,
                1});
            mhist.reset(true);
            cycle_time_range.reset();
        }
    }

    template <typename Event> void handle(Event const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

// Design notes:
//
// 1. The non-accumulating histogram_elementwise does not provide a "reset"
// input. It is not clear if this is ever needed, so leaving it out for now.
//
// 2. If the last cycle is left incomplete, no event is emitted containing the
// incomplete array of histograms. Such an event would only be useful for
// progressive display, but progressive display should (necessarily) rely on
// `element_histogram_event`.

/**
 * \brief Create a processor that computes an array of histograms over cycles
 * of batches of datapoints.
 *
 * \ingroup processors-histogram
 *
 * This is the array version of histogram_in_batches (rather than histogram).
 *
 * Each incoming bin_increment_batch_event is stored as successive elements in
 * an array of histograms. On each batch, an element_histogram_event is
 * emitted, referencing the corresponding (single) histogram.
 *
 * At the end of each cycle a histogram_array_event is emitted, referencing the
 * whole array of histograms from the cycle.
 *
 * \tparam OverflowStrategy strategy tag type to select how to handle bin
 * overflows
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c
 * bin_index_type, and \c bin_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param downstream downstream processor (moved out)
 *
 * \return histogram-array processor
 */
template <typename OverflowStrategy, typename DataTraits = default_data_traits,
          typename Downstream>
auto histogram_elementwise(std::size_t num_elements, std::size_t num_bins,
                           typename DataTraits::bin_type max_per_bin,
                           Downstream &&downstream) {
    return internal::histogram_elementwise<OverflowStrategy, DataTraits,
                                           Downstream>(
        num_elements, num_bins, max_per_bin,
        std::forward<Downstream>(downstream));
}

namespace internal {

template <typename ResetEvent, typename OverflowStrategy, bool EmitConcluding,
          typename DataTraits, typename Downstream>
class histogram_elementwise_accumulate {
  public:
    using bin_index_type = typename DataTraits::bin_index_type;
    using bin_type = typename DataTraits::bin_type;

    static_assert(
        is_any_of_v<OverflowStrategy, saturate_on_overflow, reset_on_overflow,
                    stop_on_overflow, error_on_overflow>);

    // EmitConcluding cannot be used with saturate-on-overflow because there is
    // no way to roll back the current cycle in the presense of lost counts due
    // to saturation.
    static_assert(not(EmitConcluding &&
                      std::is_same_v<OverflowStrategy, saturate_on_overflow>),
                  "EmitConcluding is incompatible with saturate_on_overflow");

    // We require EmitConcluding for reset/stop-on-overflow, because it doesn't
    // make much sense to use those overflow policies without the
    // cycle-atomic concluding array event. I don't want to increase unit test
    // code to test such cases, so disallow.
    static_assert(EmitConcluding ||
                      not is_any_of_v<reset_on_overflow, stop_on_overflow>,
                  "EmitConcluding must be true for this overflow policy");

  private:
    using internal_overflow_strategy = std::conditional_t<
        std::is_same_v<OverflowStrategy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;
    static constexpr bool need_journal =
        EmitConcluding || std::is_same_v<OverflowStrategy, reset_on_overflow>;
    using journal_type =
        std::conditional_t<need_journal,
                           bin_increment_batch_journal<bin_index_type>,
                           null_journal<bin_index_type>>;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    std::unique_ptr<bin_type[]> hist_arr_mem;
    span<bin_type> hist_arr;
    multi_histogram_accumulation<bin_index_type, bin_type,
                                 internal_overflow_strategy>
        mhista;
    histogram_stats stats;
    journal_type journal;
    abstime_range<typename DataTraits::abstime_type> cycle_time_range;
    abstime_range<typename DataTraits::abstime_type> total_time_range;
    Downstream downstream;

    void emit_concluding(bool end_of_stream) {
        assert(mhista.is_consistent());
        downstream.handle(concluding_histogram_array_event<DataTraits>{
            total_time_range, autocopy_span<bin_type>(hist_arr), stats,
            mhista.cycle_index(), end_of_stream});
    }

    [[noreturn]] void stop() {
        downstream.flush();
        throw end_processing("elementwise histogram bin overflowed");
    }

    [[noreturn]] void overflow_error(char const *msg) {
        throw histogram_overflow_error(msg);
    }

    void handle_overflow(bin_increment_batch_event<DataTraits> const &event) {
        if constexpr (std::is_same_v<OverflowStrategy, saturate_on_overflow>) {
            unreachable();
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            reset_on_overflow>) {
            if (mhista.cycle_index() == 0) {
                overflow_error(
                    "elementwise histogram bin overflowed on a single batch");
            }
            mhista.roll_back_current_cycle(journal, stats);
            if constexpr (EmitConcluding)
                emit_concluding(false);
            stats = decltype(stats){};
            total_time_range.reset();
            // Keep journal and cycle time range!
            mhista.reset_and_replay(journal, stats);
            return handle(event); // Recurse max once
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            stop_on_overflow>) {
            if constexpr (EmitConcluding) {
                mhista.roll_back_current_cycle(journal, stats);
                emit_concluding(true);
            }
            stop();
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            error_on_overflow>) {
            overflow_error("elementwise histogram bin overflowed");
        } else {
            static_assert(false_for_type<OverflowStrategy>::value);
        }
    }

  public:
    explicit histogram_elementwise_accumulate(std::size_t num_elements,
                                              std::size_t num_bins,
                                              bin_type max_per_bin,
                                              Downstream downstream)
        : hist_arr_mem(new bin_type[num_elements * num_bins]),
          hist_arr(hist_arr_mem.get(), num_elements * num_bins),
          mhista(hist_arr, max_per_bin, num_bins, num_elements, true),
          downstream(std::move(downstream)) {}

    template <typename DT>
    void handle(bin_increment_batch_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTraits::bin_index_type>);
        assert(not mhista.is_cycle_complete());
        auto element_index = mhista.next_element_index();
        if (not mhista.apply_increment_batch(event.bin_indices, stats,
                                             journal))
            return handle_overflow(event);
        cycle_time_range.extend(event.time_range);

        downstream.handle(element_histogram_event<DataTraits>{
            event.time_range, element_index,
            autocopy_span<bin_type>(mhista.element_span(element_index)), stats,
            mhista.cycle_index()});

        if (mhista.is_cycle_complete()) {
            total_time_range.extend(cycle_time_range);
            mhista.new_cycle(journal);
            downstream.handle(histogram_array_event<DataTraits>{
                total_time_range, autocopy_span<bin_type>(hist_arr), stats,
                mhista.cycle_index()});
            cycle_time_range.reset();
        }
    }

    void handle([[maybe_unused]] ResetEvent const &event) {
        if constexpr (EmitConcluding) {
            mhista.roll_back_current_cycle(journal, stats);
            emit_concluding(false);
        }
        mhista.reset(true);
        journal.clear();
        stats = decltype(stats){};
        total_time_range.reset();
        cycle_time_range.reset();
    }

    template <typename Event> void handle(Event const &event) {
        downstream.handle(event);
    }

    void flush() {
        if constexpr (EmitConcluding) {
            mhista.roll_back_current_cycle(journal, stats);
            emit_concluding(true);
        }
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that collects an array of histograms accumulated
 * over cycles of batches of datapoints.
 *
 * \ingroup processors-histogram
 *
 * The reset event \c ResetEvent causes the array of histograms to be cleared
 * and a new accumulation to be started.
 *
 * \tparam ResetEvent type of event causing histograms to reset
 *
 * \tparam OverflowStrategy strategy tag type to select how to handle bin
 * overflows
 *
 * \tparam EmitConcluding if true, emit a \c concluding_histogram_array_event
 * each time a cycle completes
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c
 * bin_index_type, and \c bin_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param downstream downstream processor (moved out)
 *
 * \return accumulate-histogram-arrays processor
 */
template <typename ResetEvent, typename OverflowStrategy,
          bool EmitConcluding = false,
          typename DataTraits = default_data_traits, typename Downstream>
auto histogram_elementwise_accumulate(
    std::size_t num_elements, std::size_t num_bins,
    typename DataTraits::bin_type max_per_bin, Downstream &&downstream) {
    return internal::histogram_elementwise_accumulate<
        ResetEvent, OverflowStrategy, EmitConcluding, DataTraits, Downstream>(
        num_elements, num_bins, max_per_bin,
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
