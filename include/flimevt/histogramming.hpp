/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "autocopy_span.hpp"
#include "common.hpp"
#include "histogram_events.hpp"

#include <gsl/span>

#include <algorithm>
#include <iterator>
#include <type_traits>

namespace flimevt::internal {

struct saturate_on_internal_overflow {
    explicit saturate_on_internal_overflow() = default;
};

struct stop_on_internal_overflow {
    explicit stop_on_internal_overflow() = default;
};

/**
 * \brief Data structure representing a log of bin increment batches.
 *
 * \tparam TBinIndex bin index type
 */
template <typename TBinIndex> class bin_increment_batch_journal {
  public:
    using bin_index_type = TBinIndex;

  private:
    std::size_t n_batches = 0; // Including empty batches.

    // Index of last non-empty batch. We use -1, not 0, as initial value so
    // that the first entry in encoded_indices has a positive delta.
    std::size_t last_stored_index = std::size_t(-1);

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
        last_stored_index = std::size_t(-1);
        encoded_indices.clear();
        all_bin_indices.clear();
    }

    /**
     * \brief Clear this journal and release memory.
     */
    void clear_and_shrink_to_fit() {
        n_batches = 0;
        last_stored_index = std::size_t(-1);

        // Optimizers work better when the clear() and shrink_to_fit() are
        // juxtaposed.
        encoded_indices.clear();
        encoded_indices.shrink_to_fit();
        all_bin_indices.clear();
        all_bin_indices.shrink_to_fit();
    }

    [[nodiscard]] bool empty() const noexcept { return n_batches == 0; }

    /**
     * \brief Append a bin increment batch to this journal.
     *
     * \tparam It random access iterator type
     * \param first iterator pointing to begining of batch (bin indices)
     * \param last iterator pointing to past-end of batch
     */
    template <typename It> void append_batch(It first, It last) {
        std::size_t const elem_index = n_batches;
        if (std::size_t batch_size = as_unsigned(std::distance(first, last));
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
    void append_batch(gsl::span<TBinIndex const> batch) {
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

        std::size_t prev_batch_index = std::size_t(-1);
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

            std::advance(bin_indices_iter, as_signed(batch_size));

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
                    std::next(bin_indices_iter, as_signed(batch_size))};
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
        return const_iterator(std::size_t(-1), encoded_indices.cbegin(),
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

    /** \brief Stream insertion operator. */
    friend std::ostream &operator<<(std::ostream &s,
                                    bin_increment_batch_journal const &j) {

        s << "journal(" << j.num_batches() << ", { ";
        for (auto [index, begin, end] : j) {
            s << '(' << index << ", ";
            internal::print_range(s, begin, end);
            s << ')';
        }
        return s << "})";
    }
};

// Can be used to disable journaling.
template <typename BinIndex> struct null_journal {
    using bin_index_type = BinIndex;
    void append_batch(
        [[maybe_unused]] gsl::span<bin_index_type const> batch) noexcept {}
    void clear() noexcept {}
    void clear_and_shrink_to_fit() noexcept {}
};

// Adapter which can attach to a span and treat it as a histogram.
template <typename BinIndex, typename Bin, typename Ovfl>
class single_histogram {
  public:
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    static_assert(is_any_of_v<Ovfl, saturate_on_internal_overflow,
                              stop_on_internal_overflow>);

  private:
    gsl::span<bin_type> hist;
    bin_type bin_max = 0;

  public:
    // Attach to 'histogram' and allow bin values up to max_per_bin.
    explicit single_histogram(gsl::span<bin_type> histogram,
                              bin_type max_per_bin) noexcept
        : hist(histogram), bin_max(max_per_bin) {}

    // We do not disallow copy/move. Copy is "valid" because we do not hold any
    // state outside of the span.

    // Clear the histogram by setting all bins to zero.
    void clear() noexcept { std::fill(hist.begin(), hist.end(), bin_type(0)); }

    bin_type max_per_bin() const noexcept { return bin_max; }

    // Increment each bin in 'increments'. Return actual number of increments
    // applied. The return value always equals increments.size() if Ovfl is
    // saturate_on_internal_overflow. Otherwise, it is any value between 0 and
    // increments.size(), inclusive.
    template <typename S>
    std::size_t apply_increments(gsl::span<bin_index_type const> increments,
                                 S &stats) noexcept {
        for (auto it = increments.begin(); it != increments.end(); ++it) {
            assert(*it >= 0 && *it < hist.size());
            bin_type &bin = hist[*it];
            if (bin < bin_max) {
                ++bin;
            } else if constexpr (std::is_same_v<
                                     Ovfl, saturate_on_internal_overflow>) {
                ++stats.saturated;
            } else if constexpr (std::is_same_v<Ovfl,
                                                stop_on_internal_overflow>) {
                return as_unsigned(std::distance(increments.begin(), it));
            } else {
                static_assert(false_for_type<Ovfl>::value);
            }
            ++stats.total;
        }
        return increments.size();
    }

    // Undo the given 'increments'. Behavior is undefined unless 'increments'
    // equal the values passed to apply_increments() in an immediately prior
    // call. Behavior undefined in saturate mode.
    template <typename S>
    void undo_increments(gsl::span<bin_index_type const> increments,
                         S &stats) noexcept {
        assert((std::is_same_v<Ovfl, stop_on_internal_overflow>));
        for (bin_index_type i : increments) {
            assert(i >= 0 && i < hist.size());
            --hist[i];
            --stats.total;
        }
    }
};

// One cycle (frame, repeat unit) of an array of histograms.
// Adapter which can attach to a span.
template <typename BinIndex, typename Bin, typename Ovfl>
class multi_histogram {
  public:
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    static_assert(is_any_of_v<Ovfl, saturate_on_internal_overflow,
                              stop_on_internal_overflow>);

  private:
    gsl::span<bin_type> hist_arr;
    std::size_t element_index = 0;
    bin_type max_per_bin = 0;
    std::size_t num_bins = 0;
    std::size_t num_elements = 0;
    bool need_to_clear = false;

  public:
    explicit multi_histogram(gsl::span<bin_type> hist_array,
                             bin_type max_per_bin, std::size_t num_bins,
                             std::size_t num_elements, bool clear) noexcept
        : hist_arr(hist_array), max_per_bin(max_per_bin), num_bins(num_bins),
          num_elements(num_elements), need_to_clear(clear) {
        assert(hist_array.size() == num_bins * num_elements);
    }

    // True if any increment batches have been applied (and not rolled back).
    bool is_started() const noexcept { return element_index > 0; }

    // True if cycle is completed (applying further increment batches is
    // incorrect).
    bool is_complete() const noexcept { return element_index >= num_elements; }

    // True if every bin of every element histogram has been initialized
    // (cleared if requested; original value accepted otherwise). The
    // hist_array data is not suitable for subsequent use unless this condition
    // has been reached. When clearing is requested, the data is consistent
    // when:
    // - All elements have had increments applied,
    // - apply_increments() returned false,
    // - skip_remaining() was called at least once, or
    // - roll_back() was called at least once.
    // When clearing is not requested, the data is also consistent when no
    // operations have been performed yet.
    bool is_consistent() const noexcept {
        return (not is_started() && not need_to_clear) || is_complete();
    }

    std::size_t next_element_index() const noexcept { return element_index; }

    gsl::span<bin_type> element_span(std::size_t index) noexcept {
        return hist_arr.subspan(num_bins * index, num_bins);
    }

    // Apply 'increments' to the next element of the array of histograms.
    template <typename S, typename J>
    bool apply_increment_batch(gsl::span<bin_index_type const> batch, S &stats,
                               J &journal) noexcept {
        static_assert(
            std::is_same_v<typename J::bin_index_type, bin_index_type>);
        assert(not is_complete());
        single_histogram<bin_index_type, bin_type, Ovfl> single_hist(
            hist_arr.subspan(num_bins * element_index, num_bins), max_per_bin);
        if (need_to_clear)
            single_hist.clear();
        auto n_applied = single_hist.apply_increments(batch, stats);
        if (n_applied == batch.size()) {
            journal.append_batch(batch);
            ++element_index;
            return true;
        } else {
            if constexpr (std::is_same_v<Ovfl,
                                         saturate_on_internal_overflow>) {
                unreachable();
            } else if constexpr (std::is_same_v<Ovfl,
                                                stop_on_internal_overflow>) {
                // Always handle increment batches atomically.
                single_hist.undo_increments(batch.first(n_applied), stats);
                skip_remaining();
                return false;
            } else {
                static_assert(false_for_type<Ovfl>::value);
            }
        }
    }

    // Call to cancel processing and ensure that the remaining elements are
    // cleared (if so requested). After the call, is_complete() and
    // is_consistent() become true.
    void skip_remaining() noexcept {
        if (need_to_clear) {
            auto remaining = hist_arr.subspan(num_bins * element_index);
            std::fill(remaining.begin(), remaining.end(), bin_type(0));
            need_to_clear = false;
        }
        element_index = num_elements;
    }

    // Roll back journaled increments and recover the array of histograms to
    // its original state (if it was not cleared) or zero. Behavior undefined
    // in saturate mode.
    template <typename J, typename S>
    void roll_back(J const &journal, S &stats) noexcept {
        static_assert(
            std::is_same_v<typename J::bin_index_type, bin_index_type>);
        assert((std::is_same_v<Ovfl, stop_on_internal_overflow>));
        for (auto [index, begin, end] : journal) {
            single_histogram<bin_index_type, bin_type, Ovfl> single_hist(
                hist_arr.subspan(num_bins * index, num_bins), max_per_bin);
            // Unlike std::span, gsl::span lacks a constructor from generic
            // contiguous iterators and only allows element pointers.
            single_hist.undo_increments({&*begin, &*end}, stats);
        }
        // Ensure the previously untouched tail of the span gets cleared, if
        // clearing was requested and has not happened yet.
        skip_remaining();
        element_index = 0;
    }

    // Replay journal. Must be in unstarted state. Previous reset (or
    // constructor) must have requested clearing, or else the span must contain
    // the same data as when the journal was constructed.
    template <typename J, typename S>
    void replay(J const &journal, S &stats) noexcept {
        static_assert(
            std::is_same_v<typename J::bin_index_type, bin_index_type>);
        assert((std::is_same_v<Ovfl, stop_on_internal_overflow>));
        assert(not is_started());
        for (auto [index, begin, end] : journal) {
            single_histogram<bin_index_type, bin_type, Ovfl> single_hist(
                hist_arr.subspan(num_bins * index, num_bins), max_per_bin);
            if (need_to_clear)
                single_hist.clear();
            auto n_applied =
                single_hist.apply_increments({&*begin, &*end}, stats);
            // Under correct usage, 'journal' only repeats previous success, so
            // cannot overflow.
            assert(n_applied ==
                   static_cast<std::size_t>(std::distance(begin, end)));
        }
        element_index = journal.num_batches();
    }

    // Reset this instance for reuse on another cycle through the array.
    void reset(bool clear) noexcept {
        element_index = 0;
        need_to_clear = clear;
    }
};

// An accumulation (over multiple cycles) of an array of histograms.
// Adapter which can attach to a span.
template <typename BinIndex, typename Bin, typename Ovfl>
class multi_histogram_accumulation {
  public:
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    static_assert(is_any_of_v<Ovfl, saturate_on_internal_overflow,
                              stop_on_internal_overflow>);

  private:
    gsl::span<bin_type> hist_arr;
    std::size_t cycle_idx = 0;
    multi_histogram<bin_index_type, bin_type, Ovfl> cur_cycle;

  public:
    explicit multi_histogram_accumulation(gsl::span<bin_type> hist_array,
                                          bin_type max_per_bin,
                                          std::size_t num_bins,
                                          std::size_t num_elements,
                                          bool clear_first) noexcept
        : hist_arr(hist_array), cur_cycle(hist_array, max_per_bin, num_bins,
                                          num_elements, clear_first) {}

    bool is_cycle_started() const noexcept { return cur_cycle.is_started(); }

    bool is_cycle_complete() const noexcept { return cur_cycle.is_complete(); }

    bool is_consistent() const noexcept { return cur_cycle.is_consistent(); }

    std::size_t next_element_index() const noexcept {
        return cur_cycle.next_element_index();
    }

    gsl::span<bin_type> element_span(std::size_t index) noexcept {
        return cur_cycle.element_span(index);
    }

    std::size_t cycle_index() const noexcept { return cycle_idx; }

    // Finish the current cycle and start a new one. Must call once after
    // each cycle of element increment batches. Passing 'journal' (which is
    // cleared) is required here to avoid forgetting to clear the journal for a
    // new cycle.
    template <typename J> void new_cycle(J &journal) noexcept {
        assert(is_cycle_complete());
        ++cycle_idx;
        cur_cycle.reset(false);
        journal.clear();
    }

    template <typename S, typename J>
    bool apply_increment_batch(gsl::span<bin_index_type const> batch, S &stats,
                               J &journal) noexcept {
        assert(not is_cycle_complete());
        return cur_cycle.apply_increment_batch(batch, stats, journal);
    }

    void skip_remainder_of_current_cycle() noexcept {
        cur_cycle.skip_remaining();
    }

    // Restores histograms and stats to state just after previous new_cycle()
    // call. Behavior undefined in saturate mode.
    template <typename J, typename S>
    void roll_back_current_cycle(J const &journal, S &stats) noexcept {
        cur_cycle.roll_back(journal, stats);
    }

    void reset(bool clear_first = true) noexcept {
        cycle_idx = 0;
        cur_cycle.reset(clear_first);
    }

    template <typename J, typename S>
    void reset_and_replay(J const &journal, S &stats) noexcept {
        reset(true);
        cur_cycle.replay(journal, stats);
    }
};

} // namespace flimevt::internal
