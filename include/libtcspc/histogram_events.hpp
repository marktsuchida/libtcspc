/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bucket.hpp"
#include "data_types.hpp"

#include <cstddef>
#include <ostream>

namespace tcspc {

/**
 * \brief Event representing a datapoint for histogramming.
 *
 * \ingroup events-binning
 *
 * \tparam DataTypes data type set specifying `datapoint_type`
 */
template <typename DataTypes = default_data_types> struct datapoint_event {
    /**
     * \brief The data type.
     */
    using datapoint_type = typename DataTypes::datapoint_type;

    /**
     * \brief The datapoint value.
     */
    datapoint_type value;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(datapoint_event const &lhs,
                                     datapoint_event const &rhs) noexcept
        -> bool {
        return lhs.value == rhs.value;
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
        return s << "datapoint(" << e.value << ')';
    }
};

/**
 * \brief Event representing data binned for histogramming.
 *
 * \ingroup events-binning
 *
 * \tparam DataTypes data type set specifying and `bin_index_type`
 */
template <typename DataTypes = default_data_types> struct bin_increment_event {
    /**
     * \brief The histogram bin index to which the data value was mapped.
     */
    typename DataTypes::bin_index_type bin_index;

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
 * \brief Event representing a cluster of data points binned for histogramming.
 *
 * \ingroup events-binning
 *
 * Typically the cluster represents some unit of data collection, such as a
 * time interval or pixel.
 *
 * \tparam DataTypes data type set specifying `bin_index_type`
 */
template <typename DataTypes = default_data_types>
struct bin_increment_cluster_event {
    /**
     * \brief The bin indices for the datapoints in the cluster.
     */
    bucket<typename DataTypes::bin_index_type> bin_indices;

    /** \brief Equality comparison operator. */
    friend auto operator==(bin_increment_cluster_event const &lhs,
                           bin_increment_cluster_event const &rhs) noexcept
        -> bool {
        return lhs.bin_indices == rhs.bin_indices;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bin_increment_cluster_event const &lhs,
                           bin_increment_cluster_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator for bin_increment_cluster_event. */
    friend auto operator<<(std::ostream &s,
                           bin_increment_cluster_event const &e)
        -> std::ostream & {
        return s << "bin_increment_cluster(" << e.bin_indices << ')';
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
 * \tparam DataTypes data type set specifying `bin_type`
 */
template <typename DataTypes = default_data_types> struct histogram_event {
    /**
     * \brief The histogram.
     */
    bucket<typename DataTypes::bin_type> data_bucket;

    /** \brief Equality comparison operator. */
    friend constexpr auto operator==(histogram_event const &lhs,
                                     histogram_event const &rhs) noexcept
        -> bool {
        return lhs.data_bucket == rhs.data_bucket;
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
        return s << "histogram(" << e.data_bucket << ')';
    }
};

/**
 * \brief Event representing the final result of histogramming.
 *
 * \ingroup events-histogram
 *
 * This event is emitted by the `tcspc::histogram()` processor once per
 * accumulation (that is, before each reset or end of stream) to provide the
 * accumulated result. The contained histogram covers only whole clusters;
 * counts from any partial cluster are not included.
 *
 * \tparam DataTypes data type set specifying `bin_type`
 */
template <typename DataTypes = default_data_types>
struct concluding_histogram_event {
    /**
     * \brief The accumulated histogram.
     */
    bucket<typename DataTypes::bin_type> data_bucket;

    /** \brief Equality comparison operator. */
    friend constexpr auto
    operator==(concluding_histogram_event const &lhs,
               concluding_histogram_event const &rhs) noexcept -> bool {
        return lhs.data_bucket == rhs.data_bucket;
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
        return s << "concluding_histogram(" << e.data_bucket << ')';
    }
};

/**
 * \brief Event representing an array of histograms.
 *
 * \ingroup events-histogram
 *
 * \tparam DataTypes data type set specifying `bin_type`
 */
template <typename DataTypes = default_data_types>
struct histogram_array_event {
    /**
     * \brief Histogram array, or view thereof.
     */
    bucket<typename DataTypes::bin_type> data_bucket;

    /** \brief Equality comparison operator. */
    friend auto operator==(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return lhs.data_bucket == rhs.data_bucket;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(histogram_array_event const &lhs,
                           histogram_array_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, histogram_array_event const &e)
        -> std::ostream & {
        return s << "histogram_array(" << e.data_bucket << ')';
    }
};

/**
 * \brief Event representing a progress update of a histogram array.
 *
 * \ingroup events-histogram
 *
 * This event is used to report updates to each element of a histogram array.
 * A view of the entire histogram array is included.
 *
 * \tparam DataTypes data type set specifying `bin_type`
 */
template <typename DataTypes = default_data_types>
struct histogram_array_progress_event {
    /**
     * \brief Size of the udpated part of the histogram array.
     */
    std::size_t valid_size;

    /**
     * \brief View of the histogram array.
     *
     * Indices 0 through `valid_size` contain data updated in the current scan.
     * The remaining indices may contain invalid data unless otherwise
     * specified.
     */
    bucket<typename DataTypes::bin_type> data_bucket;

    /** \brief Equality comparison operator. */
    friend auto operator==(histogram_array_progress_event const &lhs,
                           histogram_array_progress_event const &rhs) noexcept
        -> bool {
        return lhs.valid_size == rhs.valid_size &&
               lhs.data_bucket == rhs.data_bucket;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(histogram_array_progress_event const &lhs,
                           histogram_array_progress_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s,
                           histogram_array_progress_event const &e)
        -> std::ostream & {
        return s << "histogram_array_progress(" << e.valid_size << ", "
                 << e.data_bucket << ')';
    }
};

/**
 * \brief Event representing the final result of accumulation of a histogram
 * array.
 *
 * \ingroup events-histogram
 *
 * This event is emitted by the `tcspc::scan_histograms()` processor once per
 * round (that is, before each reset) to provide the accumulated result. The
 * contained histogram array includes only whole scans; counts from any partial
 * scan are not included.
 *
 * \tparam DataTypes data type set specifying `bin_type`
 */
template <typename DataTypes = default_data_types>
struct concluding_histogram_array_event {
    /**
     * \brief View of the histogram array.
     */
    bucket<typename DataTypes::bin_type> data_bucket;

    /** \brief Equality comparison operator. */
    friend auto
    operator==(concluding_histogram_array_event const &lhs,
               concluding_histogram_array_event const &rhs) noexcept -> bool {
        return lhs.data_bucket == rhs.data_bucket;
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
        return s << "concluding_histogram_array(" << e.data_bucket << ')';
    }
};

} // namespace tcspc
