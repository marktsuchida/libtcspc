/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bucket.hpp"
#include "common.hpp"

#include <ostream>
#include <vector>

namespace tcspc {

/**
 * \brief Event representing a datapoint for histogramming.
 *
 * \ingroup events-binning
 *
 * \tparam DataTraits traits type specifying `abstime_type` and
 * `datapoint_type`
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
 * \ingroup events-binning
 *
 * \tparam DataTraits traits type specifying and `bin_index_type`
 */
template <typename DataTraits = default_data_traits>
struct bin_increment_event {
    /**
     * \brief The histogram bin index to which the data value was mapped.
     */
    typename DataTraits::bin_index_type bin_index;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(bin_increment_event const &lhs,
                                     bin_increment_event const &rhs) noexcept
        -> bool {
        return lhs.bin_index == rhs.bin_index;
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
        return s << "bin_increment(" << e.bin_index << ')';
    }
};

/**
 * \brief Event representing a batch of data binned for histogramming.
 *
 * \ingroup events-binning
 *
 * Typically the batch represents some unit of data collection, such as a time
 * interval or pixel.
 *
 * \tparam DataTraits traits type specifying `bin_index_type`
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
        for (auto const ind : e.bin_indices)
            s << ind << ", ";
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
 * \tparam DataTraits traits type specifying `bin_type`
 */
template <typename DataTraits = default_data_traits> struct histogram_event {
    /**
     * \brief The histogram.
     */
    bucket<typename DataTraits::bin_type> bucket;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(histogram_event const &lhs,
                                     histogram_event const &rhs) noexcept
        -> bool {
        return lhs.bucket == rhs.bucket;
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
        return s << "histogram(" << e.bucket << ')';
    }
};

/**
 * \brief Event representing the final result of histogramming.
 *
 * \ingroup events-histogram
 *
 * This event is emitted by the `tcspc::histogram()` processor once per
 * accumulation (that is, before each reset or end of stream) to provide the
 * accumulated result. The contained histogram covers only whole batches;
 * counts from any partial batch are not included.
 *
 * \tparam DataTraits traits type specifying `bin_type`
 */
template <typename DataTraits = default_data_traits>
struct concluding_histogram_event {
    /**
     * \brief The accumulated histogram.
     */
    bucket<typename DataTraits::bin_type> bucket;

    /** \brief Equality comparison operator. */
    friend constexpr auto
    operator==(concluding_histogram_event const &lhs,
               concluding_histogram_event const &rhs) noexcept -> bool {
        return lhs.bucket == rhs.bucket;
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
        return s << "concluding_histogram(" << e.bucket << ')';
    }
};

/**
 * \brief Event representing an array of histograms.
 *
 * \ingroup events-histogram
 *
 * This event is used both for a series of independent arrays of histograms (as
 * with the output of the `tcspc::histogram_elementwise()` processor) and for a
 * series of updates to the same histogram array (as with the output of
 * `tcspc::histogram_elementwise_accumulate()` processor).
 *
 * \tparam DataTraits traits type specifying `bin_type`
 */
template <typename DataTraits = default_data_traits>
struct histogram_array_event {
    /**
     * \brief View of the histogram array.
     */
    bucket<typename DataTraits::bin_type> bucket;

    /** \brief Equality comparison operator. */
    friend auto operator==(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return lhs.bucket == rhs.bucket;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, histogram_array_event const &e)
        -> std::ostream & {
        return s << "histogram_array(" << e.bucket << ')';
    }
};

/**
 * \brief Event representing the final result of accumulation of a histogram
 * array.
 *
 * \ingroup events-histogram
 *
 * This event is emitted by the `tcspc::histogram_elementwise_accumulate()`
 * processor once per accumulation (that is, before each reset or end of
 * stream) to provide the accumulated result. The contained histogram array
 * covers only whole cycles; counts from any partial cycle are not included.
 *
 * \tparam DataTraits traits type specifying `bin_type`
 */
template <typename DataTraits = default_data_traits>
struct concluding_histogram_array_event {
    /**
     * \brief View of the histogram array.
     */
    bucket<typename DataTraits::bin_type> bucket;

    /** \brief Equality comparison operator. */
    friend auto
    operator==(concluding_histogram_array_event const &lhs,
               concluding_histogram_array_event const &rhs) noexcept -> bool {
        return lhs.bucket == rhs.bucket;
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
        return s << "concluding_histogram_array(" << e.bucket << ')';
    }
};

} // namespace tcspc
