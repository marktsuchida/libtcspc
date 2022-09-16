/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ostream>
#include <tuple>
#include <utility>
#include <vector>

namespace flimevt {

namespace internal {
template <typename T>
inline void print_vector(std::ostream &s, std::vector<T> const &v) {
    s << "{ ";
    for (auto const &e : v)
        s << e << ", ";
    s << "}";
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
    TBinIndex bin_index;
};

/** \brief Equality operator for bin_increment_event. */
template <typename T>
constexpr bool operator==(bin_increment_event<T> const &lhs,
                          bin_increment_event<T> const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.bin_index == rhs.bin_index;
}

/** \brief Stream insertion operator for bin_increment_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                bin_increment_event<T> const &e) {
    return s << "bin_increment(" << e.macrotime << ", " << e.bin_index << ')';
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
    std::vector<TBinIndex> bin_indices;
};

/** \brief Equality operator for bin_increment_batch_event. */
template <typename T>
// constexpr in C++20
bool operator==(bin_increment_batch_event<T> const &lhs,
                bin_increment_batch_event<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.bin_indices == rhs.bin_indices;
}

/** \brief Stream insertion operator for bin_increment_batch_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                bin_increment_batch_event<T> const &e) {
    s << "bin_increment_batch(" << e.start << ", " << e.stop << ", ";
    internal::print_vector(s, e.bin_indices);
    return s << ')';
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

namespace internal {

template <typename TBinIndex> class base_bin_increment_batch_journal_event {
    macrotime t_start = 0; // = start of first batch
    macrotime t_stop = 0;  // = stop of last batch
    std::size_t n_batches = 0;

    // Index of last non-empty batch. We use -1, not 0, as initial value so
    // that the first entry in encoded_indices has a positive delta.
    std::size_t last_stored_index = -1;

    // Delta- and run-length-encoded batch indices of stored batches, storing
    // delta (index diff since last non-empty batch) and count (batch size). If
    // delta or count exceeds 255, use extra entries:
    // E.g., delta = 300: (255, 0), (45, count)
    // E.g., count = 300: (delta, 255), (0, 45)
    // E.g., delta = 270, value = 500: (255, 0), (15, 255), (0, 245)
    std::vector<std::pair<std::uint8_t, std::uint8_t>> encoded_indices;

    // The bin indices from all batches, concatenated.
    std::vector<TBinIndex> all_bin_indices;

  public:
    // Rule of zero, default constructor.

    std::size_t num_batches() const noexcept { return n_batches; }

    macrotime start() const noexcept { return t_start; }
    macrotime stop() const noexcept { return t_stop; }
    void start(macrotime t) noexcept { t_start = t; }
    void stop(macrotime t) noexcept { t_stop = t; }

    void clear() noexcept {
        t_start = t_stop = 0;
        n_batches = 0;
        last_stored_index = -1;
        encoded_indices.clear();
        all_bin_indices.clear();
    }

    void clear_and_shrink_to_fit() {
        t_start = t_stop = 0;
        n_batches = 0;
        last_stored_index = -1;
        encoded_indices.clear();
        encoded_indices.shrink_to_fit();
        all_bin_indices.clear();
        all_bin_indices.shrink_to_fit();
    }

    void append_batch(std::vector<TBinIndex> const &bin_indices) {
        std::size_t const elem_index = n_batches;
        if (std::size_t batch_size = bin_indices.size(); batch_size > 0) {
            std::size_t delta = elem_index - last_stored_index;
            while (delta > 255) {
                encoded_indices.emplace_back(255, 0);
                delta -= 255;
            }
            while (batch_size > 255) {
                encoded_indices.emplace_back(delta, 255);
                batch_size -= 255;
                delta = 0;
            }
            encoded_indices.emplace_back(delta, batch_size);
            last_stored_index = elem_index;

            all_bin_indices.insert(all_bin_indices.end(), bin_indices.begin(),
                                   bin_indices.end());
        }
        n_batches = elem_index + 1;
    }

    class const_iterator {
        // Constant iterator yielding tuples (batch_index, bin_index_begin,
        // bin_index_end).
        //
        // Our iterator, when dereferenced, yields the value type (the above
        // tuple) directly instead of a reference. For this reason in can only
        // be an input iterator, not a forward iterator. (Forward iterators are
        // required to return a reference when dereferenced.)

        using bin_index_vector_type = std::vector<TBinIndex>;
        using encoded_index_vector_type =
            std::vector<std::pair<std::uint8_t, std::uint8_t>>;

        std::size_t prev_batch_index = -1;
        typename encoded_index_vector_type::const_iterator
            encoded_indices_iter;
        typename encoded_index_vector_type::const_iterator encoded_indices_end;
        typename bin_index_vector_type::const_iterator bin_indices_iter;

        friend class base_bin_increment_batch_journal_event;

      public:
        using value_type =
            std::tuple<std::size_t,
                       typename bin_index_vector_type::const_iterator,
                       typename bin_index_vector_type::const_iterator>;
        using difference_type = std::ptrdiff_t;
        using reference = value_type const &;
        using pointer = value_type const *;
        using iterator_category = std::input_iterator_tag;

        const_iterator &operator++() {
            assert(encoded_indices_iter != encoded_indices_end);

            for (;;) {
                prev_batch_index += encoded_indices_iter->first;
                if (encoded_indices_iter->second != 0)
                    break;
                ++encoded_indices_iter;
            }

            std::size_t batch_size = encoded_indices_iter->second;
            while (++encoded_indices_iter != encoded_indices_end &&
                   encoded_indices_iter->first == 0)
                batch_size += encoded_indices_iter->second;

            bin_indices_iter += batch_size;

            return *this;
        }

        const_iterator operator++(int) {
            const_iterator ret = *this;
            ++(*this);
            return ret;
        }

        value_type operator*() const {
            assert(encoded_indices_iter != encoded_indices_end);

            std::size_t batch_index = prev_batch_index;
            auto tmp_iter = encoded_indices_iter;
            for (;;) {
                batch_index += tmp_iter->first;
                if (tmp_iter->second != 0)
                    break;
                ++tmp_iter;
            }

            std::size_t batch_size = tmp_iter->second;
            while (++tmp_iter != encoded_indices_end && tmp_iter->first == 0)
                batch_size += tmp_iter->second;

            return {batch_index, bin_indices_iter,
                    bin_indices_iter + batch_size};
        }

        bool operator==(const_iterator other) const {
            return prev_batch_index == other.prev_batch_index &&
                   encoded_indices_iter == other.encoded_indices_iter &&
                   encoded_indices_end == other.encoded_indices_end &&
                   bin_indices_iter == other.bin_indices_iter;
        }

        bool operator!=(const_iterator other) const {
            return !(*this == other);
        }
    };

    const_iterator begin() const noexcept {
        const_iterator ret;
        ret.prev_batch_index = -1;
        ret.encoded_indices_iter = encoded_indices.cbegin();
        ret.encoded_indices_end = encoded_indices.cend();
        ret.bin_indices_iter = all_bin_indices.cbegin();
        return ret;
    }

    const_iterator end() const noexcept {
        const_iterator ret;
        ret.prev_batch_index = last_stored_index;
        ret.encoded_indices_iter = encoded_indices.cend();
        ret.encoded_indices_end = encoded_indices.cend();
        ret.bin_indices_iter = all_bin_indices.cend();
        return ret;
    }

    void swap(base_bin_increment_batch_journal_event &other) noexcept {
        using std::swap;
        swap(*this, other);
    }
};

} // namespace internal

/**
 * \brief Event carrying the journal for bin increment batches for a whole
 * frame.
 *
 * \tparam TBinIndex bin index type
 */
template <typename TBinIndex>
class bin_increment_batch_journal_event
    : public internal::base_bin_increment_batch_journal_event<TBinIndex> {};

/**
 * \brief Event carrying the journal for bin increment batches for an
 * incomplete frame.
 *
 * \tparam TBinIndex bin index type
 */
template <typename TBinIndex>
class partial_bin_increment_batch_journal_event
    : public internal::base_bin_increment_batch_journal_event<TBinIndex> {};

} // namespace flimevt
