/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "binning.hpp"
#include "common.hpp"
#include "histogram_events.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          typename D>
class histogram {
    static_assert(std::is_same_v<Ovfl, saturate_on_overflow> ||
                      std::is_same_v<Ovfl, reset_on_overflow> ||
                      std::is_same_v<Ovfl, stop_on_overflow> ||
                      std::is_same_v<Ovfl, error_on_overflow>,
                  "Not an allowed overflow strategy for histogram");

    bool started = false;  // The current accumulation has seen an increment
    bool finished = false; // No longer processing; downstream ended
    histogram_event<TBin> hist;

    TBin const max_per_bin;

    D downstream;

    void emit_concluding(bool had_data, bool end_of_stream) noexcept {
        concluding_histogram_event<TBin> e{had_data ? hist.start : 0,
                                           had_data ? hist.stop : 0,
                                           {}, // Swapped in below
                                           hist.total,
                                           hist.saturated,
                                           had_data,
                                           end_of_stream};
        e.histogram.swap(hist.histogram);
        downstream.handle_event(e);
        hist.histogram.swap(e.histogram);
    }

    void reset() noexcept {
        started = false;
        std::fill(hist.histogram.begin(), hist.histogram.end(), TBin{0});
        hist.total = 0;
        hist.saturated = 0;
    }

    void finish(std::exception_ptr error) noexcept {
        finished = true;
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
        downstream.handle_end(error);
    }

  public:
    explicit histogram(std::size_t num_bins, TBin max_per_bin, D &&downstream)
        : max_per_bin(max_per_bin), downstream(std::move(downstream)) {
        hist.histogram.resize(num_bins);
    }

    void handle_event(bin_increment_event<TBinIndex> const &event) noexcept {
        if (finished)
            return;
        bool just_started = !started;
        if (!started) {
            hist.start = event.macrotime;
            started = true;
        }

        TBin &bin = hist.histogram[event.bin_index];
        if (bin < max_per_bin) {
            ++bin;
        } else if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
            ++hist.saturated;
        } else if constexpr (std::is_same_v<Ovfl, reset_on_overflow>) {
            if (just_started) { // max_per_bin == 0
                return finish(std::make_exception_ptr(histogram_overflow_error(
                    "Histogram bin overflowed on first increment")));
            } else {
                emit_concluding(true, false);
                reset();
                return handle_event(event); // Self-recurse max once
            }
        } else if constexpr (std::is_same_v<Ovfl, stop_on_overflow>) {
            emit_concluding(!just_started, true);
            return finish({});
        } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
            return finish(std::make_exception_ptr(
                histogram_overflow_error("Histogram bin overflowed")));
        } else {
            static_assert(internal::false_for_type<Ovfl>::value);
        }

        ++hist.total;
        hist.stop = event.macrotime;
        return downstream.handle_event(hist);
    }

    void handle_event([[maybe_unused]] EReset const &event) noexcept {
        if (!finished) {
            emit_concluding(started, false);
            reset();
        }
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (!finished)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (!finished) {
            emit_concluding(started, true);
            finish(error);
        }
    }
};

} // namespace internal

/**
 * \brief Create a processor that collects a histogram of datapoints.
 *
 * Every incoming \c bin_increment_event<TBinIndex> causes the matching bin in
 * the histogram to be incremented. On every update, the current histogram is
 * emitted as a \c histogram_event<TBin> event.
 *
 * When a reset occurs (via incoming \c EReset or by overflowing when \c Ovfl
 * is reset_on_overflow), the stored histogram is cleared and restarted.
 *
 * A \c concluding_histogram_event<TBin> is emitted before each reset and
 * before successful end of stream, containing the same data as the previous \c
 * histogram_event<TBin> (or empty if there was none since the start or last
 * reset).
 *
 * Behavior is undefined if an incoming \c bin_increment_event contains a bin
 * index beyond the size of the histogram.
 *
 * \tparam TBinIndex the bin index type
 * \tparam TBin the data type of the histogram bins
 * \tparam EReset type of event causing histogram to reset
 * \tparam Ovfl strategy tag type to select how to handle bin overflows
 * \tparam D downstream processor type
 * \param num_bins number of bins in the histogram (must match the bin
 * mapper used upstream)
 * \param max_per_bin maximum value allowed in each bin
 * \param downstream downstream processor (moved out)
 * \return histogram processor
 */
template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          typename D>
auto histogram(std::size_t num_bins, TBin max_per_bin, D &&downstream) {
    return internal::histogram<TBinIndex, TBin, EReset, Ovfl, D>(
        num_bins, max_per_bin, std ::forward<D>(downstream));
}

namespace internal {

template <typename TBinIndex, typename TBin, typename Ovfl, typename D>
class histogram_in_batches {
    static_assert(std::is_same_v<Ovfl, saturate_on_overflow> ||
                      std::is_same_v<Ovfl, error_on_overflow>,
                  "Not an allowed overflow strategy for histogram_in_batches");

    bool finished = false; // No longer processing; downstream ended
    histogram_event<TBin> hist;

    TBin const max_per_bin;

    D downstream;

    void finish(std::exception_ptr error) noexcept {
        finished = true;
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
        downstream.handle_end(error);
    }

  public:
    explicit histogram_in_batches(std::size_t num_bins, TBin max_per_bin,
                                  D &&downstream)
        : max_per_bin(max_per_bin), downstream(std::move(downstream)) {
        hist.histogram.resize(num_bins);
    }

    void
    handle_event(bin_increment_batch_event<TBinIndex> const &event) noexcept {
        if (finished)
            return;

        hist.start = event.start;
        hist.stop = event.stop;
        std::fill(hist.histogram.begin(), hist.histogram.end(), TBin{0});
        hist.total = 0;
        hist.saturated = 0;

        for (auto bin_index : event.bin_indices) {
            TBin &bin = hist.histogram[bin_index];
            if (bin < max_per_bin) {
                ++bin;
            } else if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
                ++hist.saturated;
            } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
                return finish(std::make_exception_ptr(
                    histogram_overflow_error("Histogram bin overflowed")));
            } else {
                static_assert(internal::false_for_type<Ovfl>::value);
            }

            ++hist.total;
        }

        downstream.handle_event(hist);
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (!finished)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (!finished)
            finish(error);
    }
};

} // namespace internal

/**
 * \brief Create a proecssor that computes a histogram of each batch of
 * datapoints.
 *
 * Each incoming \c bin_increment_batch_event<TBinIndex> results in a \c
 * histogram_event<TBin> for that batch to be emitted.
 *
 * There is no "reset" feature because there is nothing to reset.
 *
 * Behavior is undefined if an incoming \c bin_increment_batch_event contains a
 * bin index beyond the size of the histogram.
 *
 * \tparam TBinIndex the bin index type
 * \tparam TBin the data type of the histogram bins
 * \tparam Ovfl strategy tag type to select how to handle bin overflows
 * \tparam D downstream processor type
 * \param num_bins number of bins in the histogram (must match the bin
 * mapper used upstream)
 * \param max_per_bin maximum value allowed in each bin
 * \param downstream downstream processor (moved out)
 * \return histogram-in-batches processor
 */
template <typename TBinIndex, typename TBin, typename Ovfl, typename D>
auto histogram_in_batches(std::size_t num_bins, TBin max_per_bin,
                          D &&downstream) {
    return internal::histogram_in_batches<TBinIndex, TBin, Ovfl, D>(
        num_bins, max_per_bin, std::forward<D>(downstream));
}

namespace internal {

template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          typename D>
class accumulate_histograms {
    static_assert(
        std::is_same_v<Ovfl, saturate_on_overflow> ||
            std::is_same_v<Ovfl, reset_on_overflow> ||
            std::is_same_v<Ovfl, stop_on_overflow> ||
            std::is_same_v<Ovfl, error_on_overflow>,
        "Not an allowed overflow strategy for accumulate_histograms");

    bool started = false;  // The current accumulation has seen a batch
    bool finished = false; // No longer processing; downstream ended
    histogram_event<TBin> hist;

    TBin const max_per_bin;

    D downstream;

    template <typename It>
    void roll_back_increments(It begin, It end) noexcept {
        for (auto it = begin; it != end; ++it) {
            --hist.total;
            --hist.histogram[*it];
        }
    }

    void emit_concluding(bool had_batches, bool end_of_stream) noexcept {
        concluding_histogram_event<TBin> e{had_batches ? hist.start : 0,
                                           had_batches ? hist.stop : 0,
                                           {}, // Swapped in below
                                           hist.total,
                                           hist.saturated,
                                           had_batches,
                                           end_of_stream};
        e.histogram.swap(hist.histogram);
        downstream.handle_event(e);
        hist.histogram.swap(e.histogram);
    }

    void reset() noexcept {
        started = false;
        std::fill(hist.histogram.begin(), hist.histogram.end(), TBin{0});
        hist.total = 0;
        hist.saturated = 0;
    }

    void finish(std::exception_ptr error) noexcept {
        finished = true;
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
        downstream.handle_end(error);
    }

  public:
    explicit accumulate_histograms(std::size_t num_bins, TBin max_per_bin,
                                   D &&downstream)
        : max_per_bin(max_per_bin), downstream(std::move(downstream)) {
        hist.histogram.resize(num_bins);
    }

    void
    handle_event(bin_increment_batch_event<TBinIndex> const &event) noexcept {
        if (finished)
            return;
        bool just_started = !started;
        if (!started) {
            hist.start = event.start;
            started = true;
        }

        for (auto bin_index_it = event.bin_indices.cbegin();
             bin_index_it != event.bin_indices.cend(); ++bin_index_it) {
            TBin &bin = hist.histogram[*bin_index_it];
            if (bin < max_per_bin) {
                ++bin;
            } else if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
                ++hist.saturated;
            } else if constexpr (std::is_same_v<Ovfl, reset_on_overflow>) {
                if (just_started) {
                    return finish(
                        std::make_exception_ptr(histogram_overflow_error(
                            "Histogram bin overflowed on a single batch")));
                } else {
                    roll_back_increments(event.bin_indices.cbegin(),
                                         bin_index_it);
                    emit_concluding(true, false);
                    reset();
                    return handle_event(event); // Self-recurse max once
                }
            } else if constexpr (std::is_same_v<Ovfl, stop_on_overflow>) {
                roll_back_increments(event.bin_indices.cbegin(), bin_index_it);
                emit_concluding(!just_started, true);
                return finish({});
            } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
                return finish(std::make_exception_ptr(
                    histogram_overflow_error("Histogram bin overflowed")));
            } else {
                static_assert(internal::false_for_type<Ovfl>::value);
            }

            ++hist.total;
        }

        hist.stop = event.stop;
        downstream.handle_event(hist);
    }

    void handle_event([[maybe_unused]] EReset const &event) noexcept {
        if (!finished) {
            emit_concluding(started, false);
            reset();
        }
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (!finished)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (!finished) {
            emit_concluding(started, true);
            finish(error);
        }
    }
};

} // namespace internal

/**
 * \brief Create a processor that collects a histogram accumulated over batches
 * of datapoints.
 *
 * Every incoming \c bin_increment_event<TBinIndex> is histogrammed and added
 * to the accumulated histogram. On every update, the current accumulated
 * histogram is emitted as a \c histogram_event<TBin> event.
 *
 * When a reset occurs (via incoming \c EReset or by overflowing when \c Ovfl
 * is reset_on_overflow), and when the incoming stream ends, the accumulated
 * histogram up to the previous batch (not including counts from any incomplete
 * batch) is emitted as a \c concluding_histogram_event<TBin>. (Its contents
 * are the same as the preceding \c histogram_event<TBin>, except that an empty
 * \c concluding_histogram_event<TBin> is emitted even if there were no batches
 * since the start or last reset.)
 *
 * Behavior is undefined if an incoming \c bin_increment_batch_event contains a
 * bin index beyond the size of the histogram.
 *
 * \tparam TBinIndex the bin index type
 * \tparam TBin the data type of the histogram bins
 * \tparam EReset type of event causing histogram to reset
 * \tparam Ovfl strategy tag type to select how to handle bin overflows
 * \tparam D downstream processor type
 * \param num_bins number of bins in the histogram (must match the bin
 * mapper used upstream)
 * \param max_per_bin maximum value allowed in each bin
 * \param downstream downstream processor (moved out)
 * \return accumulate-histograms processor
 */
template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          typename D>
auto accumulate_histograms(std::size_t num_bins, TBin max_per_bin,
                           D &&downstream) {
    return internal::accumulate_histograms<TBinIndex, TBin, EReset, Ovfl, D>(
        num_bins, max_per_bin, std::forward<D>(downstream));
}

} // namespace flimevt
