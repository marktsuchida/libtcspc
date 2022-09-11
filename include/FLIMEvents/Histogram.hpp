/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Binning.hpp"
#include "Common.hpp"
#include "HistogramEvents.hpp"
#include "TimeTaggedEvents.hpp"

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
struct SaturateOnOverflow {};

/**
 * \brief Histogram overflow strategy tag to request resetting the histogram
 * when a bin is about to overflow.
 */
struct ResetOnOverflow {};

/**
 * \brief Histogram overflow strategy tag to request ending the processing when
 * a bin is about to overflow.
 */
struct StopOnOverflow {};

/**
 * \brief Histogram overflow strategy tag to request treating bin overflows as
 * errors.
 */
struct ErrorOnOverflow {};

/**
 * \brief Error raised when a histogram bin overflows.
 *
 * This error is raised when the ErrorOnOverflow strategy is requested and
 * there was an overflow. It is also raised when ResetOnOverflow is requested
 * but a reset would result in an infinite loop: in the case of Histogram if
 * maximum per bin set to 0, or AccumulateHistograms if a single batch contains
 * enough increments to overflow a bin.
 */
class HistogramOverflowError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/**
 * \brief Processor that creates a histogram of datapoints.
 *
 * Every incoming \c BinIncrementEvent<TBinIndex> causes the matching bin in
 * the histogram to be incremented. On every update, the current histogram is
 * emitted as a \c HistogramEvent<TBin> event.
 *
 * When a reset occurs (via incoming \c EReset or by overflowing when \c Ovfl
 * is ResetOnOverflow), the stored histogram is cleared and restarted.
 *
 * An \c AccumulatedHistogramEvent<TBin> is emitted before each reset and
 * before successful end of stream, containing the same data as the previous \c
 * HistogramEvent<TBin> (or empty if there was none since the start or last
 * reset).
 *
 * Behavior is undefined if an incoming \c BinIncrementEvent contains a bin
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
class Histogram {
    bool started = false;  // The current accumulation has seen an increment
    bool finished = false; // No longer processing; downstream ended
    HistogramEvent<TBin> hist;

    TBin const maxPerBin;

    D downstream;

    void EmitAccumulated(bool hadData, bool endOfStream) noexcept {
        AccumulatedHistogramEvent<TBin> e;
        e.start = hadData ? hist.start : 0;
        e.stop = hadData ? hist.stop : 0;
        e.histogram.swap(hist.histogram);
        e.total = hist.total;
        e.saturated = hist.saturated;
        e.hasData = hadData;
        e.isEndOfStream = endOfStream;
        downstream.HandleEvent(e);
        hist.histogram.swap(e.histogram);
    }

    void Reset() noexcept {
        started = false;
        std::fill(hist.histogram.begin(), hist.histogram.end(), TBin{0});
        hist.total = 0;
        hist.saturated = 0;
    }

    void Finish(std::exception_ptr error) noexcept {
        finished = true;
        downstream.HandleEnd(error);
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
    }

  public:
    /**
     * \brief Construct with number of bins, maximum count, and downstream
     * processor.
     *
     * \param numBins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param maxPerBin maximum value allowed in each bin
     * \param downstream downstream processor (moved out)
     */
    explicit Histogram(std::size_t numBins, TBin maxPerBin, D &&downstream)
        : maxPerBin(maxPerBin), downstream(std::move(downstream)) {
        hist.histogram.resize(numBins);
    }

    /**
     * \brief Construct with number of bins and downstream processor.
     *
     * The maximum value allowed in each bin is set to the maximum supported by
     * \c TBin.
     *
     * \param numBins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param downstream downstream processor (moved out)
     */
    explicit Histogram(std::size_t numBins, D &&downstream)
        : Histogram(numBins, std::numeric_limits<TBin>::max(),
                    std::move(downstream)) {}

    /** \brief Processor interface **/
    void HandleEvent(BinIncrementEvent<TBinIndex> const &event) noexcept {
        if (finished)
            return;
        bool justStarted = !started;
        if (!started) {
            hist.start = event.macrotime;
            started = true;
        }

        TBin &bin = hist.histogram[event.binIndex];
        if (bin < maxPerBin) {
            ++hist.total;
            ++bin;
            hist.stop = event.macrotime;
            downstream.HandleEvent(hist);
        } else if constexpr (std::is_same_v<Ovfl, SaturateOnOverflow>) {
            ++hist.total;
            ++hist.saturated;
            hist.stop = event.macrotime;
            downstream.HandleEvent(hist);
        } else if constexpr (std::is_same_v<Ovfl, ResetOnOverflow>) {
            if (justStarted) { // maxPerBin == 0
                Finish(std::make_exception_ptr(HistogramOverflowError(
                    "Histogram bin overflowed on first increment")));
            } else {
                EmitAccumulated(true, false);
                Reset();
                HandleEvent(event);
            }
        } else if constexpr (std::is_same_v<Ovfl, StopOnOverflow>) {
            EmitAccumulated(!justStarted, true);
            Finish({});
        } else if constexpr (std::is_same_v<Ovfl, ErrorOnOverflow>) {
            Finish(std::make_exception_ptr(
                HistogramOverflowError("Histogram bin overflowed")));
        } else {
            static_assert(
                internal::FalseForType<Ovfl>::value,
                "Histogram overflow strategy must be a known tag type");
        }
    }

    /** \brief Processor interface **/
    void HandleEvent([[maybe_unused]] EReset const &event) noexcept {
        if (!finished) {
            EmitAccumulated(started, false);
            Reset();
        }
    }

    /** \brief Processor interface **/
    template <typename E> void HandleEvent(E const &event) noexcept {
        downstream.HandleEvent(event);
    }

    /** \brief Processor interface **/
    void HandleEnd(std::exception_ptr error) noexcept {
        if (!finished) {
            EmitAccumulated(started, true);
            Finish(error);
        }
    }
};

/**
 * \brief Processor that creates histograms of each batch of datapoints.
 *
 * Each incoming \c BinIncrementBatchEvent<TBinIndex> results in a \c
 * HistogramEvent<TBin> for that batch to be emitted.
 *
 * There is no "reset" feature because there is nothing to reset.
 *
 * Behavior is undefined if an incoming \c BinIncrementBatchEvent contains a
 * bin index beyond the size of the histogram.
 *
 * \tparam TBinIndex the bin index type
 * \tparam TBin the data type of the histogram bins
 * \tparam Ovfl strategy tag type to select how to handle bin overflows
 * \tparam D downstream processor type
 */
template <typename TBinIndex, typename TBin, typename Ovfl, typename D>
class HistogramInBatches {
    bool finished = false; // No longer processing; downstream ended
    HistogramEvent<TBin> hist;

    TBin const maxPerBin;

    D downstream;

    void Finish(std::exception_ptr error) noexcept {
        finished = true;
        downstream.HandleEnd(error);
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
    }

  public:
    /**
     * \brief Construct with number of bins, maximum count, and downstream
     * processor.
     *
     * \param numBins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param maxPerBin maximum value allowed in each bin
     * \param downstream downstream processor (moved out)
     */
    explicit HistogramInBatches(std::size_t numBins, TBin maxPerBin,
                                D &&downstream)
        : maxPerBin(maxPerBin), downstream(std::move(downstream)) {
        hist.histogram.resize(numBins);
    }

    /**
     * \brief Construct with number of bins and downstream processor.
     *
     * The maximum value allowed in each bin is set to the maximum supported by
     * \c TBin.
     *
     * \param numBins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param downstream downstream processor (moved out)
     */
    explicit HistogramInBatches(std::size_t numBins, D &&downstream)
        : HistogramInBatches(numBins, std::numeric_limits<TBin>::max(),
                             std::move(downstream)) {}

    /** \brief Processor interface **/
    void HandleEvent(BinIncrementBatchEvent<TBinIndex> const &event) noexcept {
        if (finished)
            return;

        hist.start = event.start;
        hist.stop = event.stop;
        std::fill(hist.histogram.begin(), hist.histogram.end(), TBin{0});
        hist.total = 0;
        hist.saturated = 0;

        for (auto binIndex : event.binIndices) {
            TBin &bin = hist.histogram[binIndex];
            if (bin < maxPerBin) {
                ++hist.total;
                ++bin;
            } else if constexpr (std::is_same_v<Ovfl, SaturateOnOverflow>) {
                ++hist.total;
                ++hist.saturated;
            } else if constexpr (std::is_same_v<Ovfl, ResetOnOverflow>) {
                static_assert(
                    internal::FalseForType<Ovfl>::value,
                    "ResetOnOverflow is not applicable to HistogramInBatches");
            } else if constexpr (std::is_same_v<Ovfl, StopOnOverflow>) {
                static_assert(
                    internal::FalseForType<Ovfl>::value,
                    "StopOnOverflow is not applicable to HistogramInBatches");
            } else if constexpr (std::is_same_v<Ovfl, ErrorOnOverflow>) {
                Finish(std::make_exception_ptr(
                    HistogramOverflowError("Histogram bin overflowed")));
                return;
            } else {
                static_assert(
                    internal::FalseForType<Ovfl>::value,
                    "Histogram overflow strategy must be a known tag type");
            }
        }

        downstream.HandleEvent(hist);
    }

    /** \brief Processor interface **/
    template <typename E> void HandleEvent(E const &event) noexcept {
        downstream.HandleEvent(event);
    }

    /** \brief Processor interface **/
    void HandleEnd(std::exception_ptr error) noexcept {
        if (!finished)
            Finish(error);
    }
};

/**
 * \brief Processor that creates a histogram accumulated over batches of
 * datapoints.
 *
 * Every incoming \c BinIncrementEvent<TBinIndex> is histogrammed and added to
 * the accumulated histogram. On every update, the current accumulated
 * histogram is emitted as a \c HistogramEvent<TBin> event.
 *
 * When a reset occurs (via incoming \c EReset or by overflowing when \c Ovfl
 * is ResetOnOverflow), and when the incoming stream ends, the accumulated
 * histogram up to the previous batch (not including counts from any incomplete
 * batch) is emitted as a \c AccumulatedHistogramEvent<TBin> event. (Its
 * contents are the same as the preceding \c HistogramEvent<TBin>, except that
 * an empty \c AccumulatedHistogramEvent<TBin> is emitted even if there were no
 * batches since the start or last reset.)
 *
 * Behavior is undefined if an incoming \c BinIncrementBatchEvent contains a
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
class AccumulateHistograms {
    bool started = false;  // The current accumulation has seen a batch
    bool finished = false; // No longer processing; downstream ended
    HistogramEvent<TBin> hist;

    TBin const maxPerBin;

    D downstream;

    template <typename It> void RollBackIncrements(It begin, It end) noexcept {
        for (auto it = begin; it != end; ++it) {
            --hist.total;
            --hist.histogram[*it];
        }
    }

    void EmitAccumulated(bool hadBatches, bool endOfStream) noexcept {
        AccumulatedHistogramEvent<TBin> e;
        e.start = hadBatches ? hist.start : 0;
        e.stop = hadBatches ? hist.stop : 0;
        e.histogram.swap(hist.histogram);
        e.total = hist.total;
        e.saturated = hist.saturated;
        e.hasData = hadBatches;
        e.isEndOfStream = endOfStream;
        downstream.HandleEvent(e);
        hist.histogram.swap(e.histogram);
    }

    void Reset() noexcept {
        started = false;
        std::fill(hist.histogram.begin(), hist.histogram.end(), TBin{0});
        hist.total = 0;
        hist.saturated = 0;
    }

    void Finish(std::exception_ptr error) noexcept {
        finished = true;
        downstream.HandleEnd(error);
        hist.histogram.clear();
        hist.histogram.shrink_to_fit();
    }

  public:
    /**
     * \brief Construct with number of bins, maximum count, and downstream
     * processor.
     *
     * \param numBins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param maxPerBin maximum value allowed in each bin
     * \param downstream downstream processor (moved out)
     */
    explicit AccumulateHistograms(std::size_t numBins, TBin maxPerBin,
                                  D &&downstream)
        : maxPerBin(maxPerBin), downstream(std::move(downstream)) {
        hist.histogram.resize(numBins);
    }

    /**
     * \brief Construct with number of bins and downstream processor.
     *
     * The maximum value allowed in each bin is set to the maximum supported by
     * \c TBin.
     *
     * \param numBins number of bins in the histogram (must match the bin
     * mapper used upstream)
     * \param downstream downstream processor (moved out)
     */
    explicit AccumulateHistograms(std::size_t numBins, D &&downstream)
        : AccumulateHistograms(numBins, std::numeric_limits<TBin>::max(),
                               std::move(downstream)) {}

    /** \brief Processor interface **/
    void HandleEvent(BinIncrementBatchEvent<TBinIndex> const &event) noexcept {
        if (finished)
            return;
        bool justStarted = !started;
        if (!started) {
            hist.start = event.start;
            started = true;
        }

        for (auto binIndexIt = event.binIndices.cbegin();
             binIndexIt != event.binIndices.cend(); ++binIndexIt) {
            TBin &bin = hist.histogram[*binIndexIt];
            if (bin < maxPerBin) {
                ++hist.total;
                ++bin;
            } else if constexpr (std::is_same_v<Ovfl, SaturateOnOverflow>) {
                ++hist.total;
                ++hist.saturated;
            } else if constexpr (std::is_same_v<Ovfl, ResetOnOverflow>) {
                if (justStarted) {
                    Finish(std::make_exception_ptr(HistogramOverflowError(
                        "Histogram bin overflowed on a single batch")));
                } else {
                    RollBackIncrements(event.binIndices.cbegin(), binIndexIt);
                    EmitAccumulated(true, false);
                    Reset();
                    HandleEvent(event);
                }
                return;
            } else if constexpr (std::is_same_v<Ovfl, StopOnOverflow>) {
                RollBackIncrements(event.binIndices.cbegin(), binIndexIt);
                EmitAccumulated(!justStarted, true);
                Finish({});
                return;
            } else if constexpr (std::is_same_v<Ovfl, ErrorOnOverflow>) {
                Finish(std::make_exception_ptr(
                    HistogramOverflowError("Histogram bin overflowed")));
                return;
            } else {
                static_assert(
                    internal::FalseForType<Ovfl>::value,
                    "Histogram overflow strategy must be a known tag type");
            }
        }

        hist.stop = event.stop;
        downstream.HandleEvent(hist);
    }

    /** \brief Processor interface **/
    void HandleEvent([[maybe_unused]] EReset const &event) noexcept {
        if (!finished) {
            EmitAccumulated(started, false);
            Reset();
        }
    }

    /** \brief Processor interface **/
    template <typename E> void HandleEvent(E const &event) noexcept {
        downstream.HandleEvent(event);
    }

    /** \brief Processor interface **/
    void HandleEnd(std::exception_ptr error) noexcept {
        if (!finished) {
            EmitAccumulated(started, true);
            Finish(error);
        }
    }
};

} // namespace flimevt
