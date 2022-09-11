/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Common.hpp"

#include <cstdint>
#include <ostream>
#include <vector>

namespace flimevt {

/**
 * \brief Event representing a datapoint for histogramming.
 *
 * \tparam TData the integer data type of the datapoint
 */
template <typename TData> struct DatapointEvent {
    /**
     * \brief The data type.
     */
    using DataType = TData;

    /**
     * \brief The macrotime of the datapoint.
     */
    Macrotime macrotime;

    /**
     * \brief The datapoint value.
     */
    DataType value;
};

/** \brief Equality operator for DatapointEvent. */
template <typename T>
constexpr bool operator==(DatapointEvent<T> const &lhs,
                          DatapointEvent<T> const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.value == rhs.value;
}

/** \brief Stream insertion operator for DatapointEvent. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s, DatapointEvent<T> const &e) {
    return s << "Datapoint(" << e.macrotime << ", " << e.value << ')';
}

/**
 * \brief Event representing data binned for histogramming.
 *
 * \tparam TBinIndex the bin index type
 */
template <typename TBinIndex> struct BinIncrementEvent {
    /**
     * \brief The macrotime of the binned datapoint.
     */
    Macrotime macrotime;

    /**
     * \brief The histogram bin index to which the data value was mapped.
     */
    TBinIndex binIndex;
};

/** \brief Equality operator for BinIncrementEvent. */
template <typename T>
constexpr bool operator==(BinIncrementEvent<T> const &lhs,
                          BinIncrementEvent<T> const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.binIndex == rhs.binIndex;
}

/** \brief Stream insertion operator for BinIncrementEvent. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                BinIncrementEvent<T> const &e) {
    return s << "Datapoint(" << e.macrotime << ", " << e.binIndex << ')';
}

/**
 * \brief Event representing a batch of data binned for histogramming.
 *
 * Typically the batch represents some unit of data collection, such as a time
 * interval or pixel.
 *
 * \tparam TBinIndex the bin index type
 */
template <typename TBinIndex> struct BinIncrementBatchEvent {
    /**
     * \brief The macrotime of the start of the batch.
     */
    Macrotime start;

    /**
     * \brief The macrotime of the end of the batch.
     */
    Macrotime stop;

    /**
     * \brief The bin indices for the datapoints in the batch.
     */
    std::vector<TBinIndex> binIndices;
};

/** \brief Equality operator for BinIncrementBatchEvent. */
template <typename T>
// constexpr in C++20
bool operator==(BinIncrementBatchEvent<T> const &lhs,
                BinIncrementBatchEvent<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.binIndices == rhs.binIndices;
}

/** \brief Stream insertion operator for BinIncrementBatchEvent. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                BinIncrementBatchEvent<T> const &e) {
    return s << "Datapoint(" << e.start << ", " << e.stop << ", "
             << e.binIndices << ')';
}

/**
 * \brief Event representing a single histogram.
 *
 * This event is used both for a series of independent histograms (as with the
 * output of HistogramInBatches) and for a series of updates to the same
 * histogram (as with the output of Histogram or AccumulateHistograms).
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct HistogramEvent {
    /**
     * \brief The macrotime of the start of the histogrammed data.
     *
     * In the output of Histogram, this is the macrotime of the first
     * histogrammed datapoint. In the output of HistogramInBatches, it is the
     * start time of the batch. In the output of AccumulateHistograms, it is
     * the start time of the first batch of the current accumulation.
     */
    Macrotime start = 0;

    /**
     * \brief The macrotime of the end of the histogrammed data.
     *
     * In the output of Histogram, this is the macrotime of the last
     * histogrammed datapoint. In the output of HistogramInBatches, it is the
     * stop time of the batch. In the output of AccumulateHistograms, it is the
     * stop time of the last batch accumulated so far.
     */
    Macrotime stop = 0;

    /**
     * \brief The histogram.
     */
    std::vector<TBin> histogram;

    /**
     * \brief The total number of datapoints histogrammed.
     *
     * This is the total of the sum of the histogram bins and the saturated
     * count. It does not include out-of-range datapoints filtered out by the
     * bin mapper.
     */
    std::uint64_t total = 0; // Includes saturated.

    /**
     * \brief The number of datapoints not recorded in the histogram due to
     * bins saturating.
     *
     * This is always zero unless the SaturateOnOverflow strategy is used.
     */
    std::uint64_t saturated = 0;
};

/** \brief Equality operator for HistogramEvent. */
template <typename T>
constexpr bool operator==(HistogramEvent<T> const &lhs,
                          HistogramEvent<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.histogram == rhs.histogram && lhs.total == rhs.total &&
           lhs.saturated == rhs.saturated;
}

namespace internal {
template <typename T>
inline void PrintVector(std::ostream &s, std::vector<T> const &v) {
    s << "{ ";
    for (auto const &e : v)
        s << e << ", ";
    s << "}";
}
} // namespace internal

/** \brief Stream insertion operator for HistogramEvent. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s, HistogramEvent<T> const &e) {
    s << "Histogram(" << e.start << ", " << e.stop << ", ";
    internal::PrintVector(s, e.histogram);
    return s << ", " << e.total << ", " << e.saturated << ')';
}

/**
 * \brief Event representing the final result of accumulating histograms.
 *
 * This event is emitted by AccumulateHistograms once per accumulation (that
 * is, before each reset or end of stream) to provide the accumulated result.
 * The contained histogram covers only whole batches; counts from any partial
 * batch are not included.
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct AccumulatedHistogramEvent {
    /**
     * \brief The macrotime of the start of the accumulation.
     *
     * This is the start time of the first batch. Only valid if hasData is
     * true.
     */
    Macrotime start = 0;

    /**
     * \brief The macrotime of the end of the accumulation.
     *
     * This is the stop time of the last batch accumulated. Only valid if
     * hasData is true.
     */
    Macrotime stop = 0;

    /**
     * \brief The accumulated histogram.
     */
    std::vector<TBin> histogram;

    /**
     * \brief The total number of datapoints histogrammed.
     *
     * This is the total of the sum of the histogram bins and the saturated
     * count. It does not include out-of-range datapoints filtered out by the
     * bin mapper.
     */
    std::uint64_t total = 0; // Includes saturated.

    /**
     * \brief The number of datapoints not recorded in the histogram due to
     * bins saturating.
     *
     * This is always zero unless the SaturateOnOverflow strategy is used.
     */
    std::uint64_t saturated = 0;

    /**
     * \brief Whether any batches were included in this accumulation.
     */
    bool hasData = false;

    /**
     * \brief Whether this accumulation ended due to end of stream.
     *
     * If this is false, the accumulation ended due to reset.
     */
    bool isEndOfStream = false;
};

/** \brief Equality operator for AccumulatedHistogramEvent. */
template <typename T>
constexpr bool operator==(AccumulatedHistogramEvent<T> const &lhs,
                          AccumulatedHistogramEvent<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.histogram == rhs.histogram && lhs.total == rhs.total &&
           lhs.saturated == rhs.saturated && lhs.hasData == rhs.hasData &&
           lhs.isEndOfStream == rhs.isEndOfStream;
}

/** \brief Stream insertion operator for AccumulatedHistogramEvent. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                AccumulatedHistogramEvent<T> const &e) {
    s << "AccumulatedHistogram(" << e.start << ", " << e.stop << ", ";
    internal::PrintVector(s, e.histogram);
    return s << ", " << e.total << ", " << e.saturated << ", " << e.hasData
             << ", " << e.isEndOfStream << ')';
}

} // namespace flimevt
