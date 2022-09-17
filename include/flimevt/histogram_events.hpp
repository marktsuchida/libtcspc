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

template <typename It>
inline void print_range(std::ostream &s, It first, It last) {
    s << "{ ";
    while (first != last)
        s << *first++ << ", ";
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
    internal::print_range(s, e.bin_indices.begin(), e.bin_indices.end());
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
    internal::print_range(s, e.histogram.begin(), e.histogram.end());
    return s << ", " << e.total << ", " << e.saturated << ')';
}

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

/** \brief Equality operator for concluding_histogram_event. */
template <typename T>
constexpr bool operator==(concluding_histogram_event<T> const &lhs,
                          concluding_histogram_event<T> const &rhs) noexcept {
    return lhs.start == rhs.start && lhs.stop == rhs.stop &&
           lhs.histogram == rhs.histogram && lhs.total == rhs.total &&
           lhs.saturated == rhs.saturated && lhs.has_data == rhs.has_data &&
           lhs.is_end_of_stream == rhs.is_end_of_stream;
}

/** \brief Stream insertion operator for concluding_histogram_event. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                concluding_histogram_event<T> const &e) {
    s << "concluding_histogram(" << e.start << ", " << e.stop << ", ";
    internal::print_range(s, e.histogram.begin(), e.histogram.end());
    return s << ", " << e.total << ", " << e.saturated << ", " << e.has_data
             << ", " << e.is_end_of_stream << ')';
}

/**
 * \brief Data structure representing a log of bin increment batches.
 *
 * \tparam TBinIndex bin index type
 */
template <typename TBinIndex> class bin_increment_batch_journal {
    std::size_t n_batches = 0; // Including empty batches.

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
    // Rule of zero

    /**
     * \brief Return the number of batches journaled.
     *
     * \return the number of batches stored
     */
    std::size_t num_batches() const noexcept { return n_batches; }

    /**
     * \brief Clear this journal.
     */
    void clear() noexcept {
        n_batches = 0;
        last_stored_index = -1;
        encoded_indices.clear();
        all_bin_indices.clear();
    }

    /**
     * \brief Clear this journal and release memory.
     */
    void clear_and_shrink_to_fit() {
        n_batches = 0;
        last_stored_index = -1;

        // Optimizers work better when the clear() and shrink_to_fit() are
        // juxtaposed.
        encoded_indices.clear();
        encoded_indices.shrink_to_fit();
        all_bin_indices.clear();
        all_bin_indices.shrink_to_fit();
    }

    /**
     * \brief Append a bin increment batch to this journal.
     *
     * \tparam It random access iterator type
     * \param first iterator pointing to begining of batch (bin indices)
     * \param last iterator pointing to past-end of batch
     */
    template <typename It> void append_batch(It first, It last) {
        std::size_t const elem_index = n_batches;
        if (std::size_t batch_size = std::distance(first, last);
            batch_size > 0) {
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

            all_bin_indices.insert(all_bin_indices.end(), first, last);
        }
        n_batches = elem_index + 1;
    }

    /**
     * \brief Append a bin increment batch to this journal.
     *
     * This is a convenience function taking a vector instead of iterator pair.
     *
     * \param batch the bin increment batch to append (bin indices)
     */
    void append_batch(std::vector<TBinIndex> const &batch) {
        append_batch(batch.begin(), batch.end());
    }

    /**
     * \brief Constant iterator for bin_increment_batch_journal.
     *
     * Satisfies the requirements for input iterator.
     *
     * The iterator, when dereferenced, yields the triple (std::tuple)
     * (batch_index, batch_begin, batch_end), where batch_index is the index
     * (std::size_t) of the batch (order appended to the journal), and
     * batch_begin and batch_end are an iterator pair pointing to the range of
     * bin indices (TBinIndex) belonging to the batch.
     *
     * For efficiency reasons, empty batches are skipped over. If you need to
     * take action for empty batches, you need to store the batch_index as you
     * iterate and check if it is consecutive.
     *
     * There is no non-const iterator because iteration is read-only.
     */
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

        friend class bin_increment_batch_journal;

        explicit const_iterator(
            std::size_t prev_batch_index,
            typename encoded_index_vector_type::const_iterator
                encoded_indices_iter,
            typename encoded_index_vector_type::const_iterator
                encoded_indices_end,
            typename bin_index_vector_type::const_iterator
                bin_indices_iter) noexcept
            : prev_batch_index(prev_batch_index),
              encoded_indices_iter(encoded_indices_iter),
              encoded_indices_end(encoded_indices_end),
              bin_indices_iter(bin_indices_iter) {}

      public:
        /** \brief Iterator value type. */
        using value_type =
            std::tuple<std::size_t,
                       typename bin_index_vector_type::const_iterator,
                       typename bin_index_vector_type::const_iterator>;

        /** \brief Iterator difference type. */
        using difference_type = std::ptrdiff_t;
        /** \brief Iterator reference type. */
        using reference = value_type const &;
        /** \brief Iterator pointer type. */
        using pointer = value_type const *;
        /** \brief Iterator category tag. */
        using iterator_category = std::input_iterator_tag;

        const_iterator() = delete;

        // Rule of zero

        /** \brief Iterator pre-increment operator. */
        const_iterator &operator++() noexcept {
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

        /** \brief Iterator post-increment operator. */
        const_iterator operator++(int) noexcept {
            const_iterator ret = *this;
            ++(*this);
            return ret;
        }

        /** \brief Iterator dereference operator. */
        value_type operator*() const noexcept {
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

        /** \brief Equality operator. */
        bool operator==(const_iterator other) const noexcept {
            return prev_batch_index == other.prev_batch_index &&
                   encoded_indices_iter == other.encoded_indices_iter &&
                   encoded_indices_end == other.encoded_indices_end &&
                   bin_indices_iter == other.bin_indices_iter;
        }

        /** \brief Inequality operator. */
        bool operator!=(const_iterator other) const noexcept {
            return !(*this == other);
        }
    };

    /**
     * \brief Return a constant iterator for the beginning of the journal.
     *
     * \return constant input iterator pointing to beginning
     */
    const_iterator begin() const noexcept {
        return const_iterator(-1, encoded_indices.cbegin(),
                              encoded_indices.cend(),
                              all_bin_indices.cbegin());
    }

    /**
     * \brief Return a constant iterator for the past-end of the journal.
     *
     * \return constant input iterator pointing to past-end
     */
    const_iterator end() const noexcept {
        return const_iterator(last_stored_index, encoded_indices.cend(),
                              encoded_indices.cend(), all_bin_indices.cend());
    }

    /**
     * \brief Swap the contents of this journal with another.
     *
     * \param other the ohter journal
     */
    void swap(bin_increment_batch_journal &other) noexcept {
        using std::swap;
        swap(*this, other);
    }

    /** \brief Equality operator. */
    bool operator==(bin_increment_batch_journal const &other) const noexcept {
        return n_batches == other.n_batches &&
               last_stored_index == other.last_stored_index &&
               encoded_indices == other.encoded_indices &&
               all_bin_indices == other.all_bin_indices;
    }
};

/** \brief Stream insertion operator for bin_increment_batch_journal. */
template <typename T>
inline std::ostream &operator<<(std::ostream &s,
                                bin_increment_batch_journal<T> const &j) {

    s << "journal(" << j.num_batches() << ", { ";
    for (auto [index, begin, end] : j) {
        s << '(' << index << ", ";
        internal::print_range(s, begin, end);
        s << ')';
    }
    return s << "})";
}

/**
 * \brief Event carrying the journal for bin increment batches for a whole
 * frame.
 *
 * \tparam TBinIndex bin index type
 */
template <typename TBinIndex> struct bin_increment_batch_journal_event {
    /**
     * \brief The macrotime of the start of the first batch recorded.
     */
    macrotime start = 0;

    /**
     * \brief The macrotime of the end of the last batch recorded.
     */
    macrotime stop = 0;

    /**
     * \brief The journal containing bin indices for each batch.
     */
    bin_increment_batch_journal<TBinIndex> journal;

    /** \brief Equality operator. */
    bool
    operator==(bin_increment_batch_journal_event const &other) const noexcept {
        return start == other.start && stop == other.stop &&
               journal == other.journal;
    }
};

/**
 * \brief Event carrying the journal for bin increment batches for an
 * incomplete frame.
 *
 * \tparam TBinIndex bin index type
 */
template <typename TBinIndex>
struct partial_bin_increment_batch_journal_event {
    /**
     * \brief The macrotime of the start of the first batch recorded.
     */
    macrotime start = 0;

    /**
     * \brief The macrotime of the end of the last batch recorded.
     */
    macrotime stop = 0;

    /**
     * \brief The journal containing bin indices for each batch.
     */
    bin_increment_batch_journal<TBinIndex> journal;

    /** \brief Equality operator. */
    bool operator==(partial_bin_increment_batch_journal_event const &other)
        const noexcept {
        return start == other.start && stop == other.stop &&
               journal == other.journal;
    }
};

/** \brief Stream insertion operator for bin_increment_batch_journal_event. */
template <typename T>
inline std::ostream &
operator<<(std::ostream &s, bin_increment_batch_journal_event<T> const &e) {
    return s << "bin_increment_batch_journal_event(" << e.start << ", "
             << e.stop << ", " << e.journal << ')';
}

/**
 * \brief Stream insertion operator for
 * partial_bin_increment_batch_journal_event.
 */
template <typename T>
inline std::ostream &
operator<<(std::ostream &s,
           partial_bin_increment_batch_journal_event<T> const &e) {
    return s << "partial_bin_increment_batch_journal_event(" << e.start << ", "
             << e.stop << ", " << e.journal << ')';
}

} // namespace flimevt
