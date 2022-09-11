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
template <typename TData> struct datapoint_event {
    /**
     * \brief The data type.
     */
    using data_type = TData;

    /**
     * \brief The macrotime of the datapoint.
     */
    macrotime macrotime;

    /**
     * \brief The datapoint value.
     */
    data_type value;
};

/** \brief Equality operator for datapoint_event. */
template <typename T>
constexpr bool operator==(datapoint_event<T> const &lhs,
                          datapoint_event<T> const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.value == rhs.value;
}

/** \brief Stream insertion operator for datapoint_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s, datapoint_event<T> const &e) {
    return s << "datapoint(" << e.macrotime << ", " << e.value << ')';
}

/**
 * \brief Event representing data binned for histogramming.
 *
 * \tparam TBinIndex the bin index type
 */
template <typename TBinIndex> struct bin_increment_event {
    /**
     * \brief The macrotime of the binned datapoint.
     */
    macrotime macrotime;

    /**
     * \brief The histogram bin index to which the data value was mapped.
     */
    TBinIndex binIndex;
};

/** \brief Equality operator for bin_increment_event. */
template <typename T>
constexpr bool operator==(bin_increment_event<T> const &lhs,
                          bin_increment_event<T> const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.binIndex == rhs.binIndex;
}

/** \brief Stream insertion operator for bin_increment_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                bin_increment_event<T> const &e) {
    return s << "bin_increment(" << e.macrotime << ", " << e.binIndex << ')';
}

/**
 * \brief Event representing a batch of data binned for histogramming.
 *
 * Typically the batch represents some unit of data collection, such as a time
 * interval or pixel.
 *
 * \tparam TBinIndex the bin index type
 */
template <typename TBinIndex> struct bin_increment_batch_event {
    /**
     * \brief The macrotime of the start of the batch.
     */
    macrotime start;

    /**
     * \brief The macrotime of the end of the batch.
     */
    macrotime stop;

    /**
     * \brief The bin indices for the datapoints in the batch.
     */
    std::vector<TBinIndex> binIndices;
};

/** \brief Equality operator for bin_increment_batch_event. */
template <typename T>
// constexpr in C++20
bool operator==(bin_increment_batch_event<T> const &lhs,
                bin_increment_batch_event<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.binIndices == rhs.binIndices;
}

/** \brief Stream insertion operator for bin_increment_batch_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                bin_increment_batch_event<T> const &e) {
    return s << "bin_increment_batch(" << e.start << ", " << e.stop << ", "
             << e.binIndices << ')';
}

/**
 * \brief Event representing a single histogram.
 *
 * This event is used both for a series of independent histograms (as with the
 * output of histogram_in_batches) and for a series of updates to the same
 * histogram (as with the output of histogram or accumulate_histograms).
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct histogram_event {
    /**
     * \brief The macrotime of the start of the histogrammed data.
     *
     * In the output of histogram, this is the macrotime of the first
     * histogrammed datapoint. In the output of histogram_in_batches, it is the
     * start time of the batch. In the output of accumulate_histograms, it is
     * the start time of the first batch of the current accumulation.
     */
    macrotime start = 0;

    /**
     * \brief The macrotime of the end of the histogrammed data.
     *
     * In the output of histogram, this is the macrotime of the last
     * histogrammed datapoint. In the output of histogram_in_batches, it is the
     * stop time of the batch. In the output of accumulate_histograms, it is
     * the stop time of the last batch accumulated so far.
     */
    macrotime stop = 0;

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
     * This is always zero unless the saturate_on_overflow strategy is used.
     */
    std::uint64_t saturated = 0;
};

/** \brief Equality operator for histogram_event. */
template <typename T>
constexpr bool operator==(histogram_event<T> const &lhs,
                          histogram_event<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.histogram == rhs.histogram && lhs.total == rhs.total &&
           lhs.saturated == rhs.saturated;
}

namespace internal {
template <typename T>
inline void print_vector(std::ostream &s, std::vector<T> const &v) {
    s << "{ ";
    for (auto const &e : v)
        s << e << ", ";
    s << "}";
}
} // namespace internal

/** \brief Stream insertion operator for histogram_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s, histogram_event<T> const &e) {
    s << "histogram(" << e.start << ", " << e.stop << ", ";
    internal::print_vector(s, e.histogram);
    return s << ", " << e.total << ", " << e.saturated << ')';
}

/**
 * \brief Event representing the final result of accumulating histograms.
 *
 * This event is emitted by accumulate_histograms once per accumulation (that
 * is, before each reset or end of stream) to provide the accumulated result.
 * The contained histogram covers only whole batches; counts from any partial
 * batch are not included.
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct accumulated_histogram_event {
    /**
     * \brief The macrotime of the start of the accumulation.
     *
     * This is the start time of the first batch. Only valid if has_data is
     * true.
     */
    macrotime start = 0;

    /**
     * \brief The macrotime of the end of the accumulation.
     *
     * This is the stop time of the last batch accumulated. Only valid if
     * has_data is true.
     */
    macrotime stop = 0;

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
     * This is always zero unless the saturate_on_overflow strategy is used.
     */
    std::uint64_t saturated = 0;

    /**
     * \brief Whether any batches were included in this accumulation.
     */
    bool has_data = false;

    /**
     * \brief Whether this accumulation ended due to end of stream.
     *
     * If this is false, the accumulation ended due to reset.
     */
    bool is_end_of_stream = false;
};

/** \brief Equality operator for accumulated_histogram_event. */
template <typename T>
constexpr bool operator==(accumulated_histogram_event<T> const &lhs,
                          accumulated_histogram_event<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.histogram == rhs.histogram && lhs.total == rhs.total &&
           lhs.saturated == rhs.saturated && lhs.has_data == rhs.has_data &&
           lhs.is_end_of_stream == rhs.is_end_of_stream;
}

/** \brief Stream insertion operator for accumulated_histogram_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                accumulated_histogram_event<T> const &e) {
    s << "accumulated_histogram(" << e.start << ", " << e.stop << ", ";
    internal::print_vector(s, e.histogram);
    return s << ", " << e.total << ", " << e.saturated << ", " << e.has_data
             << ", " << e.is_end_of_stream << ')';
}

} // namespace flimevt
