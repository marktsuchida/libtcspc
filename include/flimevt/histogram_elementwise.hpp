/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "binning.hpp"
#include "common.hpp"
#include "histogram_events.hpp"
#include "histogramming.hpp"

#include <gsl/span>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

template <typename TBinIndex, typename TBin, typename Ovfl, typename D>
class histogram_elementwise {
  public:
    using bin_index_type = TBinIndex;
    using bin_type = TBin;
    static_assert(is_any_of_v<Ovfl, saturate_on_overflow, error_on_overflow>);

  private:
    using internal_overflow_strategy =
        std::conditional_t<std::is_same_v<Ovfl, saturate_on_overflow>,
                           saturate_on_internal_overflow,
                           stop_on_internal_overflow>;

    bool finished = false;
    // We use unique_ptr<T[]> rather than vector<T> because the latter cannot
    // be allocated without zeroing.
    std::unique_ptr<bin_type[]> hist_arr_mem;
    gsl::span<bin_type> hist_arr;
    multi_histogram<bin_index_type, bin_type, internal_overflow_strategy>
        mhist;
    histogram_stats stats;
    null_journal<bin_index_type> journal; // Journaling not required
    macrotime_range cycle_time_range;
    D downstream;

    void finish(std::exception_ptr error) noexcept {
        finished = true;
        hist_arr_mem.reset();
        hist_arr = {};
        downstream.handle_end(error);
    }

  public:
    explicit histogram_elementwise(std::size_t num_elements,
                                   std::size_t num_bins, bin_type max_per_bin,
                                   D &&downstream)
        : hist_arr_mem(new bin_type[num_elements * num_bins]),
          hist_arr(hist_arr_mem.get(), num_elements * num_bins),
          mhist(hist_arr, max_per_bin, num_bins, num_elements, true),
          downstream(std::move(downstream)) {}

    void handle_event(
        bin_increment_batch_event<bin_index_type> const &event) noexcept {
        if (finished)
            return;
        assert(not mhist.is_complete());

        auto element_index = mhist.next_element_index();
        if (not mhist.apply_increment_batch(event.bin_indices, stats,
                                            journal)) {
            if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
                unreachable();
            } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
                return finish(std::make_exception_ptr(histogram_overflow_error(
                    "elementwise histogram bin overflowed")));
            } else {
                static_assert(false_for_type<Ovfl>::value);
            }
        }
        cycle_time_range.extend(event.time_range);

        auto const ehe = element_histogram_event<bin_type>{
            event.time_range, element_index,
            autocopy_span<bin_type>(mhist.element_span(element_index)), stats,
            0};
        downstream.handle_event(ehe);

        if (mhist.is_complete()) {
            auto const hae = histogram_array_event<bin_type>{
                cycle_time_range, autocopy_span<bin_type>(hist_arr), stats, 1};
            downstream.handle_event(hae);
            mhist.reset(true);
            cycle_time_range.reset();
        }
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (not finished)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (not finished)
            finish(error);
    }
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
 * This is the array version of histogram_in_batches (rather than histogram).
 *
 * Each incoming bin_increment_batch_event is stored as successive elements in
 * an array of histograms. On each batch, an element_histogram_event is
 * emitted, referencing the corresponding (single) histogram.
 *
 * At the end of each cycle a histogram_array_event is emitted, referencing the
 * whole array of histograms from the cycle.
 *
 * \tparam TBinIndex the bin index type
 * \tparam TBin the data type of the histogram bins
 * \tparam Ovfl strategy tag type to select how to handle bin overflows
 * \tparam D downstream processor type
 * \param num_elements the number of elements (each a histogram) in the array
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 * \param max_per_bin maximum value allowed in each bin
 * \param downstream downstream processor (moved out)
 * \return histogram-array processor
 */
template <typename TBinIndex, typename TBin, typename Ovfl, typename D>
auto histogram_elementwise(std::size_t num_elements, std::size_t num_bins,
                           TBin max_per_bin, D &&downstream) {
    return internal::histogram_elementwise<TBinIndex, TBin, Ovfl, D>(
        num_elements, num_bins, max_per_bin, std::forward<D>(downstream));
}

namespace internal {

template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          bool EmitConcluding, typename D>
class histogram_elementwise_accumulate {
  public:
    using bin_index_type = TBinIndex;
    using bin_type = TBin;

    static_assert(is_any_of_v<Ovfl, saturate_on_overflow, reset_on_overflow,
                              stop_on_overflow, error_on_overflow>);

    // EmitConcluding cannot be used with saturate-on-overflow because there is
    // no way to roll back the current cycle in the presense of lost counts due
    // to saturation.
    static_assert(not(EmitConcluding &&
                      std::is_same_v<Ovfl, saturate_on_overflow>),
                  "EmitConcluding is incompatible with saturate_on_overflow");

    // We require EmitConcluding for reset/stop-on-overflow, because it doesn't
    // make much sense to use those overflow policies without the
    // cycle-atomic concluding array event. I don't want to increase unit test
    // code to test such cases, so disallow.
    static_assert(not(not EmitConcluding &&
                      is_any_of_v<reset_on_overflow, stop_on_overflow>),
                  "EmitConcluding must be true for this overflow policy");

  private:
    using internal_overflow_strategy =
        std::conditional_t<std::is_same_v<Ovfl, saturate_on_overflow>,
                           saturate_on_internal_overflow,
                           stop_on_internal_overflow>;
    static constexpr bool need_journal =
        EmitConcluding || std::is_same_v<Ovfl, reset_on_overflow>;
    using journal_type =
        std::conditional_t<need_journal,
                           bin_increment_batch_journal<bin_index_type>,
                           null_journal<bin_index_type>>;

    bool finished = false;
    std::unique_ptr<bin_type[]> hist_arr_mem;
    gsl::span<bin_type> hist_arr;
    multi_histogram_accumulation<bin_index_type, bin_type,
                                 internal_overflow_strategy>
        mhista;
    histogram_stats stats;
    journal_type journal;
    macrotime_range cycle_time_range;
    macrotime_range total_time_range;
    D downstream;

    void emit_concluding(bool end_of_stream) noexcept {
        assert(mhista.is_consistent());
        auto const chae = concluding_histogram_array_event<bin_type>{
            total_time_range, autocopy_span<bin_type>(hist_arr), stats,
            mhista.cycle_index(), end_of_stream};
        downstream.handle_event(chae);
    }

    void finish(std::exception_ptr error) noexcept {
        finished = true;
        hist_arr_mem.reset();
        hist_arr = {};
        journal.clear_and_shrink_to_fit();
        downstream.handle_end(error);
    }

  public:
    explicit histogram_elementwise_accumulate(std::size_t num_elements,
                                              std::size_t num_bins,
                                              bin_type max_per_bin,
                                              D &&downstream)
        : hist_arr_mem(new bin_type[num_elements * num_bins]),
          hist_arr(hist_arr_mem.get(), num_elements * num_bins),
          mhista(hist_arr, max_per_bin, num_bins, num_elements, true),
          downstream(std::move(downstream)) {}

    void handle_event(
        bin_increment_batch_event<bin_index_type> const &event) noexcept {
        if (finished)
            return;
        assert(not mhista.is_cycle_complete());

        auto element_index = mhista.next_element_index();
        if (not mhista.apply_increment_batch(event.bin_indices, stats,
                                             journal)) {
            if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
                unreachable();
            } else if constexpr (std::is_same_v<Ovfl, reset_on_overflow>) {
                if (mhista.cycle_index() == 0) {
                    return finish(std::make_exception_ptr(histogram_overflow_error(
                        "elementwise histogram bin overflowed on a single batch")));
                }
                mhista.roll_back_current_cycle(journal, stats);
                if constexpr (EmitConcluding)
                    emit_concluding(false);
                stats = decltype(stats){};
                total_time_range.reset();
                // Keep journal and cycle time range!
                mhista.reset_and_replay(journal, stats);
                return handle_event(event); // Recurse max once
            } else if constexpr (std::is_same_v<Ovfl, stop_on_overflow>) {
                if constexpr (EmitConcluding) {
                    mhista.roll_back_current_cycle(journal, stats);
                    emit_concluding(true);
                }
                return finish({});
            } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
                return finish(std::make_exception_ptr(histogram_overflow_error(
                    "elementwise histogram bin overflowed")));
            } else {
                static_assert(false_for_type<Ovfl>::value);
            }
        }
        cycle_time_range.extend(event.time_range);

        auto const ehe = element_histogram_event<bin_type>{
            event.time_range, element_index,
            autocopy_span<bin_type>(mhista.element_span(element_index)), stats,
            mhista.cycle_index()};
        downstream.handle_event(ehe);

        if (mhista.is_cycle_complete()) {
            total_time_range.extend(cycle_time_range);
            mhista.new_cycle(journal);
            auto const hae = histogram_array_event<bin_type>{
                total_time_range, autocopy_span<bin_type>(hist_arr), stats,
                mhista.cycle_index()};
            downstream.handle_event(hae);
            cycle_time_range.reset();
        }
    }

    void handle_event([[maybe_unused]] EReset const &event) noexcept {
        if (finished)
            return;
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

    template <typename E> void handle_event(E const &event) noexcept {
        if (!finished)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (finished)
            return;
        if constexpr (EmitConcluding) {
            mhista.roll_back_current_cycle(journal, stats);
            emit_concluding(true);
        }
        finish(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that collects an array of histograms accumulated
 * over cycles of batches of datapoints.
 *
 * The reset event \c EReset causes the array of histograms to be cleared and a
 * new accumulation to be started.
 *
 * \tparam TBinIndex the bin index type
 * \tparam TBin the data type of the histogram bins
 * \tparam EReset type of event causing histograms to reset
 * \tparam Ovfl strategy tag type to select how to handle bin overflows
 * \tparam D downstream processor type
 * \param num_elements the number of elements (each a histogram) in the array
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 * \param max_per_bin maximum value allowed in each bin
 * \param downstream downstream processor (moved out)
 * \return accumulate-histogram-arrays processor
 */
template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          bool EmitConcluding, typename D>
auto histogram_elementwise_accumulate(std::size_t num_elements,
                                      std::size_t num_bins, TBin max_per_bin,
                                      D &&downstream) {
    return internal::histogram_elementwise_accumulate<TBinIndex, TBin, EReset,
                                                      Ovfl, EmitConcluding, D>(
        num_elements, num_bins, max_per_bin, std::forward<D>(downstream));
}

} // namespace flimevt
