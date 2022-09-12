/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "binning.hpp"
#include "common.hpp"
#include "histogram_events.hpp"
#include "time_tagged_events.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace flimevt {

/**
 * \brief Histogram overflow strategy tag to request saturating addition on
 * overflowed bins.
 */
struct saturate_on_overflow {
    explicit saturate_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request resetting the histogram
 * when a bin is about to overflow.
 */
struct reset_on_overflow {
    explicit reset_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request ending the processing when
 * a bin is about to overflow.
 */
struct stop_on_overflow {
    explicit stop_on_overflow() = default;
};

/**
 * \brief Histogram overflow strategy tag to request treating bin overflows as
 * errors.
 */
struct error_on_overflow {
    explicit error_on_overflow() = default;
};

/**
 * \brief Error raised when a histogram bin overflows.
 *
 * This error is raised when the error_on_overflow strategy is requested and
 * there was an overflow. It is also raised when reset_on_overflow is requested
 * but a reset would result in an infinite loop: in the case of histogram if
 * maximum per bin set to 0, or accumulate_histograms if a single batch
 * contains enough increments to overflow a bin.
 */
class histogram_overflow_error : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Processor that creates a histogram of datapoints.
 *
 * Every incoming \c bin_increment_event<TBinIndex> causes the matching bin in
 * the histogram to be incremented. On every update, the current histogram is
 * emitted as a \c histogram_event<TBin> event.
 *
 * When a reset occurs (via incoming \c EReset or by overflowing when \c Ovfl
 * is reset_on_overflow), the stored histogram is cleared and restarted.
 *
 * An \c accumulated_histogram_event<TBin> is emitted before each reset and
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
 */
template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          typename D>
class histogram {
    bool started = false;  // The current accumulation has seen an increment
    bool finished = false; // No longer processing; downstream ended
    histogram_event<TBin> hist;

    TBin const max_per_bin;

    D downstream;

    void emit_accumulated(bool had_data, bool end_of_stream) noexcept {
        accumulated_histogram_event<TBin> e;
        e.start = had_data ? hist.start : 0;
        e.stop = had_data ? hist.stop : 0;
        e.histogram.swap(hist.histogram);
        e.total = hist.total;
        e.saturated = hist.saturated;
        e.has_data = had_data;
        e.is_end_of_stream = end_of_stream;
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
        downstream.handle_end(error);
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
    }

  public:
    /**
     * \brief Construct with number of bins, maximum count, and downstream
     * processor.
     *
     * \param num_bins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param max_per_bin maximum value allowed in each bin
     * \param downstream downstream processor (moved out)
     */
    explicit histogram(std::size_t num_bins, TBin max_per_bin, D &&downstream)
        : max_per_bin(max_per_bin), downstream(std::move(downstream)) {
        hist.histogram.resize(num_bins);
    }

    /**
     * \brief Construct with number of bins and downstream processor.
     *
     * The maximum value allowed in each bin is set to the maximum supported by
     * \c TBin.
     *
     * \param num_bins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param downstream downstream processor (moved out)
     */
    explicit histogram(std::size_t num_bins, D &&downstream)
        : histogram(num_bins, std::numeric_limits<TBin>::max(),
                    std::move(downstream)) {}

    /** \brief Processor interface **/
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
            ++hist.total;
            ++bin;
            hist.stop = event.macrotime;
            downstream.handle_event(hist);
        } else if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
            ++hist.total;
            ++hist.saturated;
            hist.stop = event.macrotime;
            downstream.handle_event(hist);
        } else if constexpr (std::is_same_v<Ovfl, reset_on_overflow>) {
            if (just_started) { // max_per_bin == 0
                finish(std::make_exception_ptr(histogram_overflow_error(
                    "Histogram bin overflowed on first increment")));
            } else {
                emit_accumulated(true, false);
                reset();
                handle_event(event);
            }
        } else if constexpr (std::is_same_v<Ovfl, stop_on_overflow>) {
            emit_accumulated(!just_started, true);
            finish({});
        } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
            finish(std::make_exception_ptr(
                histogram_overflow_error("Histogram bin overflowed")));
        } else {
            static_assert(
                internal::false_for_type<Ovfl>::value,
                "Histogram overflow strategy must be a known tag type");
        }
    }

    /** \brief Processor interface **/
    void handle_event([[maybe_unused]] EReset const &event) noexcept {
        if (!finished) {
            emit_accumulated(started, false);
            reset();
        }
    }

    /** \brief Processor interface **/
    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface **/
    void handle_end(std::exception_ptr error) noexcept {
        if (!finished) {
            emit_accumulated(started, true);
            finish(error);
        }
    }
};

/**
 * \brief Processor that creates histograms of each batch of datapoints.
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
 */
template <typename TBinIndex, typename TBin, typename Ovfl, typename D>
class histogram_in_batches {
    bool finished = false; // No longer processing; downstream ended
    histogram_event<TBin> hist;

    TBin const max_per_bin;

    D downstream;

    void finish(std::exception_ptr error) noexcept {
        finished = true;
        downstream.handle_end(error);
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
    }

  public:
    /**
     * \brief Construct with number of bins, maximum count, and downstream
     * processor.
     *
     * \param num_bins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param max_per_bin maximum value allowed in each bin
     * \param downstream downstream processor (moved out)
     */
    explicit histogram_in_batches(std::size_t num_bins, TBin max_per_bin,
                                  D &&downstream)
        : max_per_bin(max_per_bin), downstream(std::move(downstream)) {
        hist.histogram.resize(num_bins);
    }

    /**
     * \brief Construct with number of bins and downstream processor.
     *
     * The maximum value allowed in each bin is set to the maximum supported by
     * \c TBin.
     *
     * \param num_bins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param downstream downstream processor (moved out)
     */
    explicit histogram_in_batches(std::size_t num_bins, D &&downstream)
        : histogram_in_batches(num_bins, std::numeric_limits<TBin>::max(),
                               std::move(downstream)) {}

    /** \brief Processor interface **/
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
                ++hist.total;
                ++bin;
            } else if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
                ++hist.total;
                ++hist.saturated;
            } else if constexpr (std::is_same_v<Ovfl, reset_on_overflow>) {
                static_assert(
                    internal::false_for_type<Ovfl>::value,
                    "reset_on_overflow is not applicable to histogram_in_batches");
            } else if constexpr (std::is_same_v<Ovfl, stop_on_overflow>) {
                static_assert(
                    internal::false_for_type<Ovfl>::value,
                    "stop_on_overflow is not applicable to histogram_in_batches");
            } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
                finish(std::make_exception_ptr(
                    histogram_overflow_error("Histogram bin overflowed")));
                return;
            } else {
                static_assert(
                    internal::false_for_type<Ovfl>::value,
                    "Histogram overflow strategy must be a known tag type");
            }
        }

        downstream.handle_event(hist);
    }

    /** \brief Processor interface **/
    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface **/
    void handle_end(std::exception_ptr error) noexcept {
        if (!finished)
            finish(error);
    }
};

/**
 * \brief Processor that creates a histogram accumulated over batches of
 * datapoints.
 *
 * Every incoming \c bin_increment_event<TBinIndex> is histogrammed and added
 * to the accumulated histogram. On every update, the current accumulated
 * histogram is emitted as a \c histogram_event<TBin> event.
 *
 * When a reset occurs (via incoming \c EReset or by overflowing when \c Ovfl
 * is reset_on_overflow), and when the incoming stream ends, the accumulated
 * histogram up to the previous batch (not including counts from any incomplete
 * batch) is emitted as a \c accumulated_histogram_event<TBin> event. (Its
 * contents are the same as the preceding \c histogram_event<TBin>, except that
 * an empty \c accumulated_histogram_event<TBin> is emitted even if there were
 * no batches since the start or last reset.)
 *
 * Behavior is undefined if an incoming \c bin_increment_batch_event contains a
 * bin index beyond the size of the histogram.
 *
 * \tparam TBinIndex the bin index type
 * \tparam TBin the data type of the histogram bins
 * \tparam EReset type of event causing histogram to reset
 * \tparam Ovfl strategy tag type to select how to handle bin overflows
 * \tparam D downstream processor type
 */
template <typename TBinIndex, typename TBin, typename EReset, typename Ovfl,
          typename D>
class accumulate_histograms {
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

    void emit_accumulated(bool had_batches, bool end_of_stream) noexcept {
        accumulated_histogram_event<TBin> e;
        e.start = had_batches ? hist.start : 0;
        e.stop = had_batches ? hist.stop : 0;
        e.histogram.swap(hist.histogram);
        e.total = hist.total;
        e.saturated = hist.saturated;
        e.has_data = had_batches;
        e.is_end_of_stream = end_of_stream;
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
        downstream.handle_end(error);
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
    }

  public:
    /**
     * \brief Construct with number of bins, maximum count, and downstream
     * processor.
     *
     * \param num_bins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param max_per_bin maximum value allowed in each bin
     * \param downstream downstream processor (moved out)
     */
    explicit accumulate_histograms(std::size_t num_bins, TBin max_per_bin,
                                   D &&downstream)
        : max_per_bin(max_per_bin), downstream(std::move(downstream)) {
        hist.histogram.resize(num_bins);
    }

    /**
     * \brief Construct with number of bins and downstream processor.
     *
     * The maximum value allowed in each bin is set to the maximum supported by
     * \c TBin.
     *
     * \param num_bins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param downstream downstream processor (moved out)
     */
    explicit accumulate_histograms(std::size_t num_bins, D &&downstream)
        : accumulate_histograms(num_bins, std::numeric_limits<TBin>::max(),
                                std::move(downstream)) {}

    /** \brief Processor interface **/
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
                ++hist.total;
                ++bin;
            } else if constexpr (std::is_same_v<Ovfl, saturate_on_overflow>) {
                ++hist.total;
                ++hist.saturated;
            } else if constexpr (std::is_same_v<Ovfl, reset_on_overflow>) {
                if (just_started) {
                    finish(std::make_exception_ptr(histogram_overflow_error(
                        "Histogram bin overflowed on a single batch")));
                } else {
                    roll_back_increments(event.bin_indices.cbegin(),
                                         bin_index_it);
                    emit_accumulated(true, false);
                    reset();
                    handle_event(event);
                }
                return;
            } else if constexpr (std::is_same_v<Ovfl, stop_on_overflow>) {
                roll_back_increments(event.bin_indices.cbegin(), bin_index_it);
                emit_accumulated(!just_started, true);
                finish({});
                return;
            } else if constexpr (std::is_same_v<Ovfl, error_on_overflow>) {
                finish(std::make_exception_ptr(
                    histogram_overflow_error("Histogram bin overflowed")));
                return;
            } else {
                static_assert(
                    internal::false_for_type<Ovfl>::value,
                    "Histogram overflow strategy must be a known tag type");
            }
        }

        hist.stop = event.stop;
        downstream.handle_event(hist);
    }

    /** \brief Processor interface **/
    void handle_event([[maybe_unused]] EReset const &event) noexcept {
        if (!finished) {
            emit_accumulated(started, false);
            reset();
        }
    }

    /** \brief Processor interface **/
    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface **/
    void handle_end(std::exception_ptr error) noexcept {
        if (!finished) {
            emit_accumulated(started, true);
            finish(error);
        }
    }
};

} // namespace flimevt
