/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "autocopy_span.hpp"
#include "common.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace flimevt {

/** \brief Basic statistics for histograms and histogram arrays. */
struct histogram_stats {
    /**
     * \brief Total frequency count of the histogram or histogram array.
     *
     * This count includes any count that was excluded from the histogram or
     * histogram array due to bin saturation.
     */
    std::uint64_t total = 0;

    /**
     * \brief Number of counts that were excluded due to bin saturation.
     *
     * This count is included in the total. It is the difference between the
     * actual total of all bins (of all emenets, if applicable) and the \c
     * total field of this struct.
     */
    std::uint64_t saturated = 0;

    /** \brief Equality comparison operator. */
    friend auto operator==(histogram_stats const &lhs,
                           histogram_stats const &rhs) noexcept -> bool {
        return lhs.total == rhs.total && lhs.saturated == rhs.saturated;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(histogram_stats const &lhs,
                           histogram_stats const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, histogram_stats stats)
        -> std::ostream & {
        return s << '{' << stats.total << ", " << stats.saturated << '}';
    }
};

/** \brief A range of macrotime. */
struct macrotime_range {
    /** \brief Value indicating unset start time. */
    static constexpr auto unstarted = std::numeric_limits<macrotime>::max();

    /** \brief Value indicating unset stop time. */
    static constexpr auto unstopped = std::numeric_limits<macrotime>::min();

    /** \brief Start time of the range. */
    macrotime start = unstarted;

    /** \brief Stop time of the range. */
    macrotime stop = unstopped;

    /** \brief Reset to empty time range. */
    constexpr void reset() noexcept {
        start = unstarted;
        stop = unstopped;
    }

    /** \brief Extend the time range to include the given timestamp. */
    constexpr void extend(macrotime timestamp) noexcept {
        start = std::min(start, timestamp);
        stop = std::max(stop, timestamp);
    }

    /** \brief Extend the time range to include the given time range. */
    constexpr void extend(macrotime other_start,
                          macrotime other_stop) noexcept {
        start = std::min(start, other_start);
        stop = std::max(stop, other_stop);
    }

    /** \brief Extend the time range to include the given time range. */
    constexpr void extend(macrotime_range const &other) noexcept {
        extend(other.start, other.stop);
    }

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(macrotime_range lhs,
                                     macrotime_range rhs) noexcept -> bool {
        return lhs.start == rhs.start && lhs.stop == rhs.stop;
    }

    /** \brief Inequality comparison operator. */
    friend constexpr auto operator!=(macrotime_range lhs,
                                     macrotime_range rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, macrotime_range r)
        -> std::ostream & {
        return s << '{' << r.start << ", " << r.stop << '}';
    }
};

namespace internal {

template <typename It>
inline void print_range(std::ostream &s, It first, It last) {
    using raw_type = std::remove_reference_t<decltype(*first)>;
    static_assert(std::is_integral_v<raw_type>);
    using int_type = std::conditional_t<std::is_unsigned_v<raw_type>,
                                        std::uint64_t, std::int64_t>;
    s << "{ ";
    while (first != last)
        s << int_type{*first++} << ", ";
    s << '}';
}

} // namespace internal

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

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(datapoint_event const &lhs,
                                     datapoint_event const &rhs) noexcept
        -> bool {
        return lhs.macrotime == rhs.macrotime && lhs.value == rhs.value;
    }

    /** \brief Inequality comparison operator. */
    friend constexpr auto operator!=(datapoint_event const &lhs,
                                     datapoint_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, datapoint_event const &e)
        -> std::ostream & {
        return s << "datapoint(" << e.macrotime << ", " << e.value << ')';
    }
};

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
    TBinIndex bin_index;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(bin_increment_event const &lhs,
                                     bin_increment_event const &rhs) noexcept
        -> bool {
        return lhs.macrotime == rhs.macrotime &&
               lhs.bin_index == rhs.bin_index;
    }

    /** \brief Inequality comparison operator. */
    friend constexpr auto operator!=(bin_increment_event const &lhs,
                                     bin_increment_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, bin_increment_event const &e)
        -> std::ostream & {
        return s << "bin_increment(" << e.macrotime << ", " << e.bin_index
                 << ')';
    }
};

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
     * \brief The macrotime range of the batch.
     */
    macrotime_range time_range;

    /**
     * \brief The bin indices for the datapoints in the batch.
     */
    std::vector<TBinIndex> bin_indices;

    /** \brief Equality comparison operator. */
    friend auto operator==(bin_increment_batch_event const &lhs,
                           bin_increment_batch_event const &rhs) noexcept
        -> bool {
        return lhs.time_range == rhs.time_range &&
               lhs.bin_indices == rhs.bin_indices;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bin_increment_batch_event const &lhs,
                           bin_increment_batch_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator for bin_increment_batch_event. */
    friend auto operator<<(std::ostream &s, bin_increment_batch_event const &e)
        -> std::ostream & {
        s << "bin_increment_batch(" << e.time_range << ", ";
        internal::print_range(s, e.bin_indices.begin(), e.bin_indices.end());
        return s << ')';
    }
};

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
     * \brief The macrotime range of the histogrammed data.
     */
    macrotime_range time_range;

    /**
     * \brief The histogram.
     */
    autocopy_span<TBin> histogram;

    /** \brief Statistics. */
    histogram_stats stats;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(histogram_event const &lhs,
                                     histogram_event const &rhs) noexcept
        -> bool {
        return lhs.time_range == rhs.time_range &&
               lhs.histogram == rhs.histogram && lhs.stats == rhs.stats;
    }

    /** \brief Inequality comparison operator. */
    friend constexpr auto operator!=(histogram_event const &lhs,
                                     histogram_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, histogram_event const &e)
        -> std::ostream & {
        s << "histogram(" << e.time_range << ", ";
        internal::print_range(s, e.histogram.span().begin(),
                              e.histogram.span().end());
        return s << ", " << e.stats << ')';
    }
};

/**
 * \brief Event representing the final result of histogramming.
 *
 * This event is emitted by histogram and accumulate_histograms once per
 * accumulation (that is, before each reset or end of stream) to provide the
 * accumulated result. The contained histogram covers only whole batches;
 * counts from any partial batch are not included.
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct concluding_histogram_event {
    /**
     * \brief The macrotime range of the histogrammed data.
     */
    macrotime_range time_range;

    /**
     * \brief The accumulated histogram.
     */
    autocopy_span<TBin> histogram;

    /** \brief Statistics. */
    histogram_stats stats;

    /**
     * \brief Number of cycles accumulated.
     *
     * This is always zero when this event is emitted from \c histogram.
     */
    std::uint64_t cycle_count = 0;

    /**
     * \brief Whether this accumulation ended due to end of stream.
     *
     * If this is false, the accumulation ended due to reset. This is useful
     * when resets are performed periodically and special treatment is required
     * for any leftover accumulation after the last reset.
     */
    bool is_end_of_stream = false;

    /** \brief Equality comparison operator. */
    friend constexpr auto
    operator==(concluding_histogram_event const &lhs,
               concluding_histogram_event const &rhs) noexcept -> bool {
        return lhs.time_range == rhs.time_range &&
               lhs.histogram == rhs.histogram && lhs.stats == rhs.stats &&
               lhs.cycle_count == rhs.cycle_count &&
               lhs.is_end_of_stream == rhs.is_end_of_stream;
    }

    /** \brief Inequality comparison operator. */
    friend constexpr auto
    operator!=(concluding_histogram_event const &lhs,
               concluding_histogram_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s,
                           concluding_histogram_event const &e)
        -> std::ostream & {
        s << "concluding_histogram(" << e.time_range << ", ";
        internal::print_range(s, e.histogram.span().begin(),
                              e.histogram.span().end());
        return s << ", " << e.stats << ", " << e.cycle_count << ", "
                 << e.is_end_of_stream << ')';
    }
};

/**
 * \brief Event representing an element histogram update in an array of
 * histograms.
 *
 * This event is used to provide progressive (elementwise) results for
 * histogram arrays. The data it references is not owned by the event, and must
 * be copied if needed after event handling returns.
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct element_histogram_event {
    /**
     * \brief The macrotime range of the histogrammed data.
     *
     * This is the time range of the bin increment batch that produced this
     * event. Note that it is the time range only of the latest batch even if
     * the histogram represents accumulated data.
     */
    macrotime_range time_range;

    /**
     * \brief The index of the element (histogram) within the array.
     */
    std::size_t element_index = 0;

    /**
     * \brief View of the histogram data.
     */
    autocopy_span<TBin> histogram;

    /** \brief Statistics (for the histogram array). */
    histogram_stats stats;

    /**
     * \brief Index of the cycle to which this element histogram belongs.
     */
    std::uint64_t cycle_index = 0;

    /** \brief Equality comparison operator. */
    friend auto operator==(element_histogram_event const &lhs,
                           element_histogram_event const &rhs) noexcept
        -> bool {
        return lhs.time_range == rhs.time_range &&
               lhs.element_index == rhs.element_index &&
               lhs.histogram == rhs.histogram && lhs.stats == rhs.stats &&
               lhs.cycle_index == rhs.cycle_index;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(element_histogram_event const &lhs,
                           element_histogram_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, element_histogram_event e)
        -> std::ostream & {
        s << "element_histogram(" << e.time_range << ", " << e.element_index
          << ", ";
        internal::print_range(s, e.histogram.span().begin(),
                              e.histogram.span().end());
        return s << ", " << e.stats << ", " << e.cycle_index << ")";
    }
};

/**
 * \brief Event representing an array of histograms.
 *
 * This even is used both for a series of independent arrays of histograms (as
 * with the output of histogram_array) and for a series of updates to the same
 * histogram array (as with the output of accumulate_histogram_arrays).
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct histogram_array_event {
    /**
     * \brief The macrotime range of the histogrammed data.
     *
     * This is the time range from the start time of the first batch of the
     * first cycle to the stop time of the last batch of the last cycle of the
     * accumulation (or single cycle).
     */
    macrotime_range time_range;

    /**
     * \brief View of the histogram array.
     */
    autocopy_span<TBin> histogram_array;

    /** \brief Statistics. */
    histogram_stats stats;

    /**
     * \brief Number of cycles accumulated.
     */
    std::uint64_t cycles_accumulated = 0;

    /** \brief Equality comparison operator. */
    friend auto operator==(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return lhs.time_range == rhs.time_range &&
               lhs.histogram_array == rhs.histogram_array &&
               lhs.stats == rhs.stats &&
               lhs.cycles_accumulated == rhs.cycles_accumulated;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, histogram_array_event e)
        -> std::ostream & {
        s << "histogram_array(" << e.time_range << ", ";
        internal::print_range(s, e.histogram_array.span().begin(),
                              e.histogram_array.span().end());
        return s << ", " << e.stats << ", " << e.cycles_accumulated << ")";
    }
};

/**
 * \brief
 *
 * This event is emitted by accumulate_histogram_arrays once per accumulation
 * (that is, before each reset or end of stream) to provide the accumulated
 * result. The contained histogram array covers only whole cycles; counts from
 * any partial cycle are not included.
 *
 * \tparam TBin the data type of the histogram bins
 */
template <typename TBin> struct concluding_histogram_array_event {
    /**
     * \brief The macrotime range of the accumulation.
     *
     * This is the time range from the start time of the first batch of the
     * first cycle to the stop time of the last batch of the last cycle of the
     * accumulation.
     */
    macrotime_range time_range;

    /**
     * \brief View of the histogram array.
     */
    autocopy_span<TBin> histogram_array;

    /** \brief Statistics. */
    histogram_stats stats;

    /**
     * \brief Number of cycles accumulated.
     */
    std::uint64_t cycles_accumulated = 0;

    /**
     * \brief Whether this accumulation neded due to end of stream.
     *
     * If this is false, the accumulation ended due to reset. This is useful
     * when resets are performed periodically and special treatment is required
     * for any leftover accumulation after the last reset.
     */
    bool is_end_of_stream = false;

    /** \brief Equality comparison operator. */
    friend auto
    operator==(concluding_histogram_array_event const &lhs,
               concluding_histogram_array_event const &rhs) noexcept -> bool {
        return lhs.time_range == rhs.time_range &&
               lhs.histogram_array == rhs.histogram_array &&
               lhs.stats == rhs.stats &&
               lhs.cycles_accumulated == rhs.cycles_accumulated &&
               lhs.is_end_of_stream == rhs.is_end_of_stream;
    }

    /** \brief Inequality comparison operator. */
    friend auto
    operator!=(concluding_histogram_array_event const &lhs,
               concluding_histogram_array_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, concluding_histogram_array_event e)
        -> std::ostream & {
        s << "concluding_histogram_array(" << e.time_range << ", ";
        internal::print_range(s, e.histogram_array.span().begin(),
                              e.histogram_array.span().end());
        return s << ", " << e.stats << ", " << e.cycles_accumulated << ", "
                 << e.is_end_of_stream << ")";
    }
};

} // namespace flimevt
