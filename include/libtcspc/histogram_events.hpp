/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "own_on_copy_view.hpp"

#include <cstdint>
#include <ostream>
#include <type_traits>
#include <vector>

namespace tcspc {

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
 * \tparam DataTraits traits type specifying \c bin_index_type
 */
template <typename DataTraits = default_data_traits>
struct bin_increment_batch_event {
    /**
     * \brief The bin indices for the datapoints in the batch.
     */
    std::vector<typename DataTraits::bin_index_type> bin_indices;

    /** \brief Equality comparison operator. */
    friend auto operator==(bin_increment_batch_event const &lhs,
                           bin_increment_batch_event const &rhs) noexcept
        -> bool {
        return lhs.bin_indices == rhs.bin_indices;
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
        s << "bin_increment_batch(";
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
 * \tparam DataTraits traits type specifying \c bin_type
 */
template <typename DataTraits = default_data_traits> struct histogram_event {
    /**
     * \brief The histogram.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(histogram_event const &lhs,
                                     histogram_event const &rhs) noexcept
        -> bool {
        return lhs.histogram == rhs.histogram;
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
        s << "histogram(";
        internal::print_range(s, e.histogram.as_span().begin(),
                              e.histogram.as_span().end());
        return s << ')';
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
 * \tparam DataTraits traits type specifying \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct concluding_histogram_event {
    /**
     * \brief The accumulated histogram.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram;

    /** \brief Equality comparison operator. */
    friend constexpr auto
    operator==(concluding_histogram_event const &lhs,
               concluding_histogram_event const &rhs) noexcept -> bool {
        return lhs.histogram == rhs.histogram;
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
        s << "concluding_histogram(";
        internal::print_range(s, e.histogram.as_span().begin(),
                              e.histogram.as_span().end());
        return s << ')';
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
 * \tparam DataTraits traits type specifying \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct element_histogram_event {
    /**
     * \brief View of the histogram data.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram;

    /** \brief Equality comparison operator. */
    friend auto operator==(element_histogram_event const &lhs,
                           element_histogram_event const &rhs) noexcept
        -> bool {
        return lhs.histogram == rhs.histogram;
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
        s << "element_histogram(";
        internal::print_range(s, e.histogram.as_span().begin(),
                              e.histogram.as_span().end());
        return s << ')';
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
 * \tparam DataTraits traits type specifying \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct histogram_array_event {
    /**
     * \brief View of the histogram array.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram_array;

    /** \brief Equality comparison operator. */
    friend auto operator==(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return lhs.histogram_array == rhs.histogram_array;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, histogram_array_event const &e)
        -> std::ostream & {
        s << "histogram_array(";
        internal::print_range(s, e.histogram_array.as_span().begin(),
                              e.histogram_array.as_span().end());
        return s << ')';
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
 * \tparam DataTraits traits type specifying \c bin_type
 */
template <typename DataTraits = default_data_traits>
struct concluding_histogram_array_event {
    /**
     * \brief View of the histogram array.
     */
    own_on_copy_view<typename DataTraits::bin_type> histogram_array;

    /** \brief Equality comparison operator. */
    friend auto
    operator==(concluding_histogram_array_event const &lhs,
               concluding_histogram_array_event const &rhs) noexcept -> bool {
        return lhs.histogram_array == rhs.histogram_array;
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
        s << "concluding_histogram_array(";
        internal::print_range(s, e.histogram_array.as_span().begin(),
                              e.histogram_array.as_span().end());
        return s << ')';
    }
};

} // namespace tcspc
