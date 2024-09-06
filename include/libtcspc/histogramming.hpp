/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "span.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <limits>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc::internal {

struct saturate_on_internal_overflow {
    explicit saturate_on_internal_overflow() = default;
};

struct stop_on_internal_overflow {
    explicit stop_on_internal_overflow() = default;
};

template <typename BinIndex> class bin_increment_cluster_journal {
  public:
    using bin_index_type = BinIndex;

  private:
    using batch_size_type = std::make_unsigned_t<bin_index_type>;
    static_assert(sizeof(batch_size_type) <= sizeof(std::size_t));

    static constexpr auto batch_size_max =
        std::numeric_limits<batch_size_type>::max();

    static constexpr auto large_batch_size_element_count =
        sizeof(std::size_t) / sizeof(batch_size_type);

    static constexpr std::size_t batch_size_mask{batch_size_max};

    // Size-prefixed batches. Before every batch (contiguous bin indices),
    // including empty batches, the size of the batch is encoding in one or
    // more elements (interpreted as batch_size_type). A single element is used
    // if the batch size is less than batch_size_max. Otherwise a multi-element
    // encoding is used, with the first element containing batch_size_max,
    // followed by the split bits of the size in low-to-high order, spanning 8
    // bytes worth of space (i.e., 1, 2, 4, or 8 elements depending on
    // batch_size_type).
    std::vector<bin_index_type> journ;

  public:
    // Slow, not for use in production. For use by tests.
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return std::size_t(std::distance(begin(), end()));
    }

    [[nodiscard]] auto empty() const noexcept -> bool { return journ.empty(); }

    void clear() noexcept { journ.clear(); }

    void append_batch(span<BinIndex const> batch) {
        std::size_t size = batch.size();
        auto encode = [](std::size_t s) {
            assert(s <= batch_size_max);
            return static_cast<bin_index_type>(
                static_cast<batch_size_type>(s));
        };
        if (size < batch_size_max) {
            journ.push_back(encode(size));
        } else {
            journ.push_back(batch_size_max);
            for (std::size_t i = 0; i < large_batch_size_element_count; ++i) {
                journ.push_back(encode(size & batch_size_mask));
                size >>= 8 * sizeof(batch_size_type);
            }
        }
        journ.insert(journ.end(), batch.begin(), batch.end());
    }

    // Constant input iterator over the batches. There is no non-const
    // iteration support. Dereferencing yields `span<bin_index_type>`
    // (including for empty batches).
    class const_iterator {
        typename std::vector<bin_index_type>::const_iterator it;

        auto increment_batch_range() const
            -> std::pair<decltype(it), decltype(it)> {
            auto start = it;
            std::size_t const batch_size = [&start] {
                if (*start == batch_size_max) {
                    ++start;
                    std::size_t s = 0;
                    for (std::size_t i = 0; i < large_batch_size_element_count;
                         ++i) {
                        std::size_t const elem{
                            static_cast<batch_size_type>(*start++)};
                        s += elem << (8 * sizeof(batch_size_type) * i);
                    }
                    return s;
                }
                return std::size_t{*start++};
            }();
            auto const stop = std::next(start, std::ptrdiff_t(batch_size));
            return {start, stop};
        }

        explicit const_iterator(decltype(it) iter) : it(iter) {}

        friend class bin_increment_cluster_journal;

      public:
        using value_type = span<bin_index_type const>;
        using difference_type = std::ptrdiff_t;
        using reference = value_type const &;
        using pointer = value_type const *;
        using iterator_category = std::input_iterator_tag;

        const_iterator() = delete;

        auto operator++() -> const_iterator & {
            auto const [start, stop] = increment_batch_range();
            it = stop;
            return *this;
        }

        auto operator++(int) -> const_iterator {
            auto const ret = *this;
            ++(*this);
            return ret;
        }

        auto operator*() const -> value_type {
            auto const [start, stop] = increment_batch_range();
            return span(&*start, &*stop);
        }

        auto operator==(const_iterator rhs) const noexcept -> bool {
            return it == rhs.it;
        }

        auto operator!=(const_iterator rhs) const noexcept -> bool {
            return not(*this == rhs);
        }
    };

    auto begin() const noexcept -> const_iterator {
        return const_iterator(journ.begin());
    }

    auto end() const noexcept -> const_iterator {
        return const_iterator(journ.end());
    }

    auto operator==(bin_increment_cluster_journal const &rhs) const noexcept
        -> bool {
        return journ == rhs.journ;
    }

    auto operator!=(bin_increment_cluster_journal const &rhs) const noexcept
        -> bool {
        return not(*this == rhs);
    }

    friend auto
    operator<<(std::ostream &s,
               bin_increment_cluster_journal const &j) -> std::ostream & {
        s << "journal(";
        for (auto const batch : j) {
            s << '{';
            for (auto const i : batch)
                s << i << ", ";
            s << "}, ";
        }
        return s << ')';
    }
};

// Can be used to disable journaling.
template <typename BinIndex> struct null_journal {
    using bin_index_type = BinIndex;
    void append_batch(span<bin_index_type const> /* batch */) {}
    void clear() noexcept {}
};

// Adapter which can attach to a span and treat it as a histogram.
template <typename BinIndex, typename Bin, typename OverflowPolicy>
class single_histogram {
  public:
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    static_assert(is_any_of_v<OverflowPolicy, saturate_on_internal_overflow,
                              stop_on_internal_overflow>);

  private:
    span<bin_type> hist;
    bin_type bin_max = 0;
    std::size_t n_bins{};

  public:
    // Attach to 'histogram' and allow bin values up to max_per_bin.
    explicit single_histogram(span<bin_type> histogram,
                              arg::max_per_bin<bin_type> max_per_bin,
                              arg::num_bins<std::size_t> num_bins) noexcept
        : hist(histogram), bin_max(max_per_bin.value), n_bins(num_bins.value) {
    }

    // Reconstruct with new span.
    explicit single_histogram(span<bin_type> histogram,
                              single_histogram const &params) noexcept
        : single_histogram(histogram, arg::max_per_bin{params.bin_max},
                           arg::num_bins{params.n_bins}) {}

    [[nodiscard]] auto num_bins() const noexcept -> std::size_t {
        return n_bins;
    }

    // Clear the histogram by setting all bins to zero.
    void clear() noexcept { std::fill(hist.begin(), hist.end(), bin_type(0)); }

    auto max_per_bin() const noexcept -> bin_type { return bin_max; }

    // Increment each bin in 'increments'. Return n_applied, the actual number
    // of increments applied without saturation. This value is between 0 and
    // increments.size(), inclusive.
    auto
    apply_increments(span<bin_index_type const> increments) -> std::size_t {
        assert(not hist.empty());
        std::size_t n_applied = 0;
        for (auto it = increments.begin(); it != increments.end(); ++it) {
            assert(*it >= 0 && *it < hist.size());
            bin_type &bin = hist[*it];
            if (bin < bin_max) {
                ++bin;
                ++n_applied;
            } else if constexpr (std::is_same_v<
                                     OverflowPolicy,
                                     saturate_on_internal_overflow>) {
                continue;
            } else if constexpr (std::is_same_v<OverflowPolicy,
                                                stop_on_internal_overflow>) {
                return n_applied;
            } else {
                static_assert(false_for_type<OverflowPolicy>::value);
            }
        }
        return n_applied;
    }

    // Undo the given 'increments'. Behavior is undefined unless 'increments'
    // equal the values passed to apply_increments() in an immediately prior
    // call. Not available in saturate mode.
    void undo_increments(span<bin_index_type const> increments) {
        static_assert(
            std::is_same_v<OverflowPolicy, stop_on_internal_overflow>);
        assert(not hist.empty());
        for (bin_index_type i : increments) {
            assert(i >= 0 && i < hist.size());
            --hist[i];
        }
    }
};

// One scan (frame, cycle, or repeat unit) of an array of histograms. Adapter
// which can attach to a span.
template <typename BinIndex, typename Bin, typename OverflowPolicy>
class multi_histogram {
  public:
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    static_assert(is_any_of_v<OverflowPolicy, saturate_on_internal_overflow,
                              stop_on_internal_overflow>);

  private:
    span<bin_type> hist_arr;
    std::size_t element_index = 0;
    bin_type bin_max = 0;
    std::size_t n_bins = 0;
    std::size_t n_elements = 0;
    bool need_to_clear = false;

  public:
    explicit multi_histogram(span<bin_type> hist_array,
                             arg::max_per_bin<bin_type> max_per_bin,
                             arg::num_bins<std::size_t> num_bins,
                             arg::num_elements<std::size_t> num_elements,
                             bool clear) noexcept
        : hist_arr(hist_array), bin_max(max_per_bin.value),
          n_bins(num_bins.value), n_elements(num_elements.value),
          need_to_clear(clear) {
        assert(hist_array.empty() || hist_array.size() == n_bins * n_elements);
    }

    // Reconstruct with new span.
    explicit multi_histogram(span<bin_type> hist_array,
                             multi_histogram const &params,
                             bool clear) noexcept
        : multi_histogram(hist_array, arg::max_per_bin{params.bin_max},
                          arg::num_bins{params.n_bins},
                          arg::num_elements{params.n_elements}, clear) {}

    [[nodiscard]] auto max_per_bin() const noexcept -> bin_type {
        return bin_max;
    }

    [[nodiscard]] auto num_bins() const noexcept -> std::size_t {
        return n_bins;
    }

    [[nodiscard]] auto num_elements() const noexcept -> std::size_t {
        return n_elements;
    }

    // True if any increment batches have been applied (and not rolled back).
    [[nodiscard]] auto is_started() const noexcept -> bool {
        return element_index > 0;
    }

    // True if scan is completed (no further increment batches may be applied).
    [[nodiscard]] auto is_complete() const noexcept -> bool {
        return element_index >= n_elements;
    }

    // True if every bin of every element histogram has been initialized
    // (cleared if requested; original value accepted otherwise). The
    // hist_array data is not suitable for subsequent use unless this condition
    // has been reached. When clearing is requested, the data is consistent
    // when:
    // - All elements have had increments applied,
    // - apply_increment_batch() returned false,
    // - skip_remaining() was called at least once, or
    // - roll_back() was called at least once.
    // When clearing is not requested, the data is also consistent when no
    // operations have been performed yet.
    [[nodiscard]] auto is_consistent() const noexcept -> bool {
        return (not is_started() && not need_to_clear) || is_complete();
    }

    [[nodiscard]] auto next_element_index() const noexcept -> std::size_t {
        return element_index;
    }

    // Apply 'batch' to the next element of the array of histograms. Return
    // true if the entire batch could be applied (without saturation); false if
    // there was saturation or we stopped and rolled back.
    template <typename Journal>
    auto apply_increment_batch(span<bin_index_type const> batch,
                               Journal &journal) -> bool {
        static_assert(
            std::is_same_v<typename Journal::bin_index_type, bin_index_type>);
        assert(not hist_arr.empty());
        assert(not is_complete());
        single_histogram<bin_index_type, bin_type, OverflowPolicy> single_hist(
            hist_arr.subspan(n_bins * element_index, n_bins),
            arg::max_per_bin{bin_max}, arg::num_bins{n_bins});
        if (need_to_clear)
            single_hist.clear();

        auto n_applied = single_hist.apply_increments(batch);

        if constexpr (std::is_same_v<OverflowPolicy,
                                     saturate_on_internal_overflow>) {
            journal.append_batch(batch);
            ++element_index;
            return n_applied == batch.size();
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            stop_on_internal_overflow>) {
            if (n_applied == batch.size()) {
                journal.append_batch(batch);
                ++element_index;
                return true;
            }
            // Always handle increment batches atomically.
            single_hist.undo_increments(batch.first(n_applied));
            skip_remaining();
            return false;
        } else {
            static_assert(false_for_type<OverflowPolicy>::value);
        }
    }

    // Call to cancel processing and ensure that the remaining elements are
    // cleared (if so requested). After the call, is_complete() and
    // is_consistent() become true.
    void skip_remaining() {
        assert(not hist_arr.empty());
        if (need_to_clear) {
            auto remaining = hist_arr.subspan(n_bins * element_index);
            std::fill(remaining.begin(), remaining.end(), bin_type(0));
            need_to_clear = false;
        }
        element_index = n_elements;
    }

    // Roll back journaled increments and recover the array of histograms to
    // its original state (if it was not cleared) or zero. Not available in
    // saturate mode.
    template <typename Journal> void roll_back(Journal const &journal) {
        static_assert(
            std::is_same_v<typename Journal::bin_index_type, bin_index_type>);
        static_assert(
            std::is_same_v<OverflowPolicy, stop_on_internal_overflow>);
        assert(not hist_arr.empty());
        std::size_t batch_index = 0;
        for (auto const batch : journal) {
            single_histogram<bin_index_type, bin_type, OverflowPolicy>
                single_hist(hist_arr.subspan(n_bins * batch_index, n_bins),
                            arg::max_per_bin{bin_max}, arg::num_bins{n_bins});
            single_hist.undo_increments(batch);
            ++batch_index;
        }
        // Ensure the previously untouched tail of the span gets cleared, if
        // clearing was requested and has not happened yet.
        skip_remaining();
        element_index = 0;
    }

    // Replay journal. Must be in unstarted state. Previous reset (or
    // constructor) must have requested clearing, or else the span must contain
    // the same data as when the journal was constructed. Not available in
    // saturate mode.
    template <typename Journal> void replay(Journal const &journal) {
        static_assert(
            std::is_same_v<typename Journal::bin_index_type, bin_index_type>);
        static_assert(
            std::is_same_v<OverflowPolicy, stop_on_internal_overflow>);
        assert(not hist_arr.empty());
        assert(not is_started());
        std::size_t batch_index = 0;
        for (auto const batch : journal) {
            single_histogram<bin_index_type, bin_type, OverflowPolicy>
                single_hist(hist_arr.subspan(n_bins * batch_index, n_bins),
                            arg::max_per_bin{bin_max}, arg::num_bins{n_bins});
            if (need_to_clear)
                single_hist.clear();
            [[maybe_unused]] auto n_applied =
                single_hist.apply_increments(batch);
            // Under correct usage, 'journal' only repeats previous success, so
            // cannot overflow.
            assert(n_applied == static_cast<std::size_t>(batch.size()));
            ++batch_index;
        }
        element_index = batch_index;
    }

    // Reset this instance for reuse on another scan through the array.
    void reset(bool clear) noexcept {
        element_index = 0;
        need_to_clear = clear;
    }
};

// An accumulation, over multiple scans, of an array of histograms. Adapter
// which can attach to a span.
template <typename BinIndex, typename Bin, typename OverflowPolicy>
class multi_histogram_accumulation {
  public:
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    static_assert(is_any_of_v<OverflowPolicy, saturate_on_internal_overflow,
                              stop_on_internal_overflow>);

  private:
    std::size_t scan_idx = 0;
    multi_histogram<bin_index_type, bin_type, OverflowPolicy> cur_scan;

  public:
    explicit multi_histogram_accumulation(
        span<bin_type> hist_array, arg::max_per_bin<bin_type> max_per_bin,
        arg::num_bins<std::size_t> num_bins,
        arg::num_elements<std::size_t> num_elements, bool clear_first) noexcept
        : cur_scan(hist_array, max_per_bin, num_bins, num_elements,
                   clear_first) {}

    // Reconstruct with new span.
    explicit multi_histogram_accumulation(
        span<bin_type> hist_array, multi_histogram_accumulation const &params,
        bool clear_first) noexcept
        : multi_histogram_accumulation(
              hist_array, arg::max_per_bin{params.max_per_bin()},
              arg::num_bins{params.num_bins()},
              arg::num_elements{params.num_elements()}, clear_first) {}

    [[nodiscard]] auto max_per_bin() const noexcept -> bin_type {
        return cur_scan.max_per_bin();
    }

    [[nodiscard]] auto num_bins() const noexcept -> std::size_t {
        return cur_scan.num_bins();
    }

    [[nodiscard]] auto num_elements() const noexcept -> std::size_t {
        return cur_scan.num_elements();
    }

    [[nodiscard]] auto is_scan_started() const noexcept -> bool {
        return cur_scan.is_started();
    }

    [[nodiscard]] auto is_scan_complete() const noexcept -> bool {
        return cur_scan.is_complete();
    }

    [[nodiscard]] auto is_consistent() const noexcept -> bool {
        return cur_scan.is_consistent();
    }

    [[nodiscard]] auto next_element_index() const noexcept -> std::size_t {
        return cur_scan.next_element_index();
    }

    [[nodiscard]] auto scan_index() const noexcept -> std::size_t {
        return scan_idx;
    }

    // Finish the current scan and start a new one. Must call once after each
    // scan. Passing 'journal' (which is cleared) is required here to avoid
    // forgetting to clear the journal for a new scan.
    template <typename Journal>
    void new_scan(Journal &journal, bool clear = false) {
        assert(is_scan_complete());
        ++scan_idx;
        cur_scan.reset(clear);
        journal.clear();
    }

    template <typename Journal>
    auto apply_increment_batch(span<bin_index_type const> batch,
                               Journal &journal) -> bool {
        assert(not is_scan_complete());
        return cur_scan.apply_increment_batch(batch, journal);
    }

    void skip_remainder_of_current_scan() { cur_scan.skip_remaining(); }

    // Restores histograms to state just after previous new_scan() call.
    // Behavior undefined in saturate mode.
    template <typename Journal>
    void roll_back_current_scan(Journal const &journal) {
        cur_scan.roll_back(journal);
    }

    void reset(bool clear_first = true) noexcept {
        scan_idx = 0;
        cur_scan.reset(clear_first);
    }

    template <typename Journal> void replay(Journal const &journal) {
        cur_scan.replay(journal);
    }
};

} // namespace tcspc::internal
