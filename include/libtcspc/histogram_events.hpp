/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "own_on_copy_view.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <type_traits>
#include <vector>

namespace tcspc {

/**
 * \brief Basic statistics for histograms and histogram arrays.
 *
 * \ingroup misc
 */
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
    friend auto operator<<(std::ostream &s, histogram_stats const &stats)
        -> std::ostream & {
        return s << '{' << stats.total << ", " << stats.saturated << '}';
    }
};

/**
 * \brief A range of abstime.
 *
 * \ingroup misc
 *
 * \tparam Abstime integer type for absolute time
 */
template <typename Abstime> struct abstime_range {
    /** \brief Value indicating unset start time. */
    static constexpr auto unstarted = std::numeric_limits<Abstime>::max();

    /** \brief Value indicating unset stop time. */
    static constexpr auto unstopped = std::numeric_limits<Abstime>::min();

    /** \brief Start time of the range. */
    Abstime start = unstarted;

    /** \brief Stop time of the range. */
    Abstime stop = unstopped;

    /** \brief Reset to empty time range. */
    constexpr void reset() noexcept {
        start = unstarted;
        stop = unstopped;
    }

    /** \brief Extend the time range to include the given timestamp. */
    constexpr void extend(Abstime timestamp) noexcept {
        start = std::min(start, timestamp);
        stop = std::max(stop, timestamp);
    }

    /** \brief Extend the time range to include the given time range. */
    constexpr void extend(arg_start<Abstime> other_start,
                          arg_stop<Abstime> other_stop) noexcept {
        start = std::min(start, other_start.value);
        stop = std::max(stop, other_stop.value);
    }

    /** \brief Extend the time range to include the given time range. */
    constexpr void extend(abstime_range const &other) noexcept {
        extend(arg_start{other.start}, arg_stop{other.stop});
    }

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(abstime_range lhs,
                                     abstime_range rhs) noexcept -> bool {
        return lhs.start == rhs.start && lhs.stop == rhs.stop;
    }

    /** \brief Inequality comparison operator. */
    friend constexpr auto operator!=(abstime_range lhs,
                                     abstime_range rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, abstime_range r)
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
 * \ingroup events-histogram
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * datapoint_type
 */
template <typename DataTraits = default_data_traits> struct datapoint_event {
    /**
     * \brief The data type.
     */
    using datapoint_type = typename DataTraits::datapoint_type;

    /**
     * \brief The abstime of the datapoint.
     */
    typename DataTraits::abstime_type abstime;

    /**
     * \brief The datapoint value.
     */
    datapoint_type value;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(datapoint_event const &lhs,
                                     datapoint_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime && lhs.value == rhs.value;
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
        return s << "datapoint(" << e.abstime << ", " << e.value << ')';
    }
};

/**
 * \brief Event representing data binned for histogramming.
 *
 * \ingroup events-histogram
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * bin_index_type
 */
template <typename DataTraits = default_data_traits>
struct bin_increment_event {
    /**
     * \brief The abstime of the binned datapoint.
     */
    typename DataTraits::abstime_type abstime;

    /**
     * \brief The histogram bin index to which the data value was mapped.
     */
    typename DataTraits::bin_index_type bin_index;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(bin_increment_event const &lhs,
                                     bin_increment_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime && lhs.bin_index == rhs.bin_index;
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
        return s << "bin_increment(" << e.abstime << ", " << e.bin_index
                 << ')';
    }
};

/**
 * \brief Event representing a batch of data binned for histogramming.
 *
 * \ingroup events-histogram
 *
 * Typically the batch represents some unit of data collection, such as a time
 * interval or pixel.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * bin_index_type
 */
template <typename DataTraits = default_data_traits>
struct bin_increment_batch_event {
    /**
     * \brief The abstime range of the batch.
     */
    abstime_range<typename DataTraits::abstime_type> time_range;

    /**
     * \brief The bin indices for the datapoints in the batch.
     */
    std::vector<typename DataTraits::bin_index_type> bin_indices;

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
 * \ingroup events-histogram
 *
 * This event may be used both for a series of independent histograms and for a
 * series of updates to the same histogram.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c bin_type
 */
template <typename DataTraits = default_data_traits> struct histogram_event {
    /**
     * \brief The abstime range of the histogrammed data.
     */
    abstime_range<typename DataTraits::abstime_type> time_range;

    /**
     * \brief The histogram.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram;

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
        internal::print_range(s, e.histogram.as_span().begin(),
                              e.histogram.as_span().end());
        return s << ", " << e.stats << ')';
    }
};

/**
 * \brief Event representing the final result of histogramming.
 *
 * \ingroup events-histogram
 *
 * This event is emitted by histogram and accumulate_histograms once per
 * accumulation (that is, before each reset or end of stream) to provide the
 * accumulated result. The contained histogram covers only whole batches;
 * counts from any partial batch are not included.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct concluding_histogram_event {
    /**
     * \brief The abstime range of the histogrammed data.
     */
    abstime_range<typename DataTraits::abstime_type> time_range;

    /**
     * \brief The accumulated histogram.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram;

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
        internal::print_range(s, e.histogram.as_span().begin(),
                              e.histogram.as_span().end());
        return s << ", " << e.stats << ", " << e.cycle_count << ", "
                 << e.is_end_of_stream << ')';
    }
};

/**
 * \brief Event representing an element histogram update in an array of
 * histograms.
 *
 * \ingroup events-histogram
 *
 * This event is used to provide progressive (elementwise) results for
 * histogram arrays. The data it references is not owned by the event, and must
 * be copied if needed after event handling returns.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct element_histogram_event {
    /**
     * \brief The abstime range of the histogrammed data.
     *
     * This is the time range of the bin increment batch that produced this
     * event. Note that it is the time range only of the latest batch even if
     * the histogram represents accumulated data.
     */
    abstime_range<typename DataTraits::abstime_type> time_range;

    /**
     * \brief The index of the element (histogram) within the array.
     */
    std::size_t element_index = 0;

    /**
     * \brief View of the histogram data.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram;

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
    friend auto operator<<(std::ostream &s, element_histogram_event const &e)
        -> std::ostream & {
        s << "element_histogram(" << e.time_range << ", " << e.element_index
          << ", ";
        internal::print_range(s, e.histogram.as_span().begin(),
                              e.histogram.as_span().end());
        return s << ", " << e.stats << ", " << e.cycle_index << ")";
    }
};

/**
 * \brief Event representing an array of histograms.
 *
 * \ingroup events-histogram
 *
 * This even is used both for a series of independent arrays of histograms (as
 * with the output of \ref histogram_elementwise) and for a series of updates
 * to the same histogram array (as with the output of \ref
 * histogram_elementwise_accumulate).
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct histogram_array_event {
    /**
     * \brief The abstime range of the histogrammed data.
     *
     * This is the time range from the start time of the first batch of the
     * first cycle to the stop time of the last batch of the last cycle of the
     * accumulation (or single cycle).
     */
    abstime_range<typename DataTraits::abstime_type> time_range;

    /**
     * \brief View of the histogram array.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram_array;

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
    friend auto operator<<(std::ostream &s, histogram_array_event const &e)
        -> std::ostream & {
        s << "histogram_array(" << e.time_range << ", ";
        internal::print_range(s, e.histogram_array.as_span().begin(),
                              e.histogram_array.as_span().end());
        return s << ", " << e.stats << ", " << e.cycles_accumulated << ")";
    }
};

/**
 * \brief Event representing the final result of accumulation of a histogram
 * array.
 *
 * \ingroup events-histogram
 *
 * This event is emitted by \ref histogram_elementwise_accumulate once per
 * accumulation (that is, before each reset or end of stream) to provide the
 * accumulated result. The contained histogram array covers only whole cycles;
 * counts from any partial cycle are not included.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct concluding_histogram_array_event {
    /**
     * \brief The abstime range of the accumulation.
     *
     * This is the time range from the start time of the first batch of the
     * first cycle to the stop time of the last batch of the last cycle of the
     * accumulation.
     */
    abstime_range<typename DataTraits::abstime_type> time_range;

    /**
     * \brief View of the histogram array.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram_array;

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
    friend auto operator<<(std::ostream &s,
                           concluding_histogram_array_event const &e)
        -> std::ostream & {
        s << "concluding_histogram_array(" << e.time_range << ", ";
        internal::print_range(s, e.histogram_array.as_span().begin(),
                              e.histogram_array.as_span().end());
        return s << ", " << e.stats << ", " << e.cycles_accumulated << ", "
                 << e.is_end_of_stream << ")";
    }
};

} // namespace tcspc
