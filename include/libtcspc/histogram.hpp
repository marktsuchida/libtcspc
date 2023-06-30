/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "binning.hpp"
#include "common.hpp"
#include "histogram_events.hpp"
#include "histogramming.hpp"
#include "span.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename BinIndex, typename Bin, typename ResetEvent,
          typename OverflowStrategy, typename Downstream>
class histogram {
  public:
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    static_assert(
        is_any_of_v<OverflowStrategy, saturate_on_overflow, reset_on_overflow,
                    stop_on_overflow, error_on_overflow>);

  private:
    using internal_overflow_strategy = std::conditional_t<
        std::is_same_v<OverflowStrategy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    bool finished = false;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    std::unique_ptr<bin_type[]> hist_mem;
    span<bin_type> hist;
    single_histogram<bin_index_type, bin_type, internal_overflow_strategy>
        shist;
    histogram_stats stats;
    macrotime_range time_range;
    Downstream downstream;

    void emit_concluding(bool end_of_stream) noexcept {
        auto const che = concluding_histogram_event<Bin>{
            time_range, autocopy_span<bin_type>(hist), stats, 0,
            end_of_stream};
        downstream.handle_event(che);
    }

    void reset() noexcept {
        shist.clear();
        stats = {};
        time_range.reset();
    }

    void finish(std::exception_ptr const &error) noexcept {
        finished = true;
        hist_mem.reset();
        downstream.handle_end(error);
    }

  public:
    explicit histogram(std::size_t num_bins, Bin max_per_bin,
                       Downstream &&downstream)
        : hist_mem(new bin_type[num_bins]), hist(hist_mem.get(), num_bins),
          shist(hist, max_per_bin), downstream(std::move(downstream)) {
        shist.clear();
    }

    void handle_event(bin_increment_event<BinIndex> const &event) noexcept {
        if (finished)
            return;

        if (not shist.apply_increments({&event.bin_index, 1}, stats)) {
            if constexpr (std::is_same_v<OverflowStrategy,
                                         saturate_on_overflow>) {
                unreachable();
            } else if constexpr (std::is_same_v<OverflowStrategy,
                                                reset_on_overflow>) {
                if (shist.max_per_bin() == 0) {
                    return finish(std::make_exception_ptr(
                        histogram_overflow_error("histogram bin overflowed")));
                }
                emit_concluding(false);
                reset();
                return handle_event(event); // Recurse max once
            } else if constexpr (std::is_same_v<OverflowStrategy,
                                                stop_on_overflow>) {
                emit_concluding(true);
                return finish({});
            } else if constexpr (std::is_same_v<OverflowStrategy,
                                                error_on_overflow>) {
                return finish(std::make_exception_ptr(
                    histogram_overflow_error("histogram bin overflowed")));
            } else {
                static_assert(false_for_type<OverflowStrategy>::value);
            }
        }
        time_range.extend(event.macrotime);
        auto const he = histogram_event<bin_type>{
            time_range, autocopy_span<Bin>(hist), stats};
        downstream.handle_event(he);
    }

    void handle_event([[maybe_unused]] ResetEvent const &event) noexcept {
        if (finished)
            return;
        emit_concluding(false);
        reset();
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        if (!finished)
            downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        if (finished)
            return;
        emit_concluding(true);
        finish(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that collects a histogram of datapoints.
 *
 * \ingroup processors-histogram
 *
 * Every incoming \c bin_increment_event<BinIndex> causes the matching bin in
 * the histogram to be incremented. On every update, the current histogram is
 * emitted as a \c histogram_event<Bin> event.
 *
 * When a reset occurs (via incoming \c ResetEvent or by overflowing when \c
 * OverflowStrategy is reset_on_overflow), the stored histogram is cleared and
 * restarted.
 *
 * A \c concluding_histogram_event<Bin> is emitted before each reset and
 * before successful end of stream, containing the same data as the previous \c
 * histogram_event<Bin> (or empty if there was none since the start or last
 * reset).
 *
 * Behavior is undefined if an incoming \c bin_increment_event contains a bin
 * index beyond the size of the histogram.
 *
 * \tparam BinIndex the bin index type
 *
 * \tparam Bin the data type of the histogram bins
 *
 * \tparam ResetEvent type of event causing histogram to reset
 *
 * \tparam OverflowStrategy strategy tag type to select how to handle bin
 * overflows
 *
 * \tparam Downstream downstream processor type
 *
 * \param num_bins number of bins in the histogram (must match the bin mapper
 * used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param downstream downstream processor (moved out)
 *
 * \return histogram processor
 */
template <typename BinIndex, typename Bin, typename ResetEvent,
          typename OverflowStrategy, typename Downstream>
auto histogram(std::size_t num_bins, Bin max_per_bin,
               Downstream &&downstream) {
    return internal::histogram<BinIndex, Bin, ResetEvent, OverflowStrategy,
                               Downstream>(
        num_bins, max_per_bin, std ::forward<Downstream>(downstream));
}

} // namespace tcspc