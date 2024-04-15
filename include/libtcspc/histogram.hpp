/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bucket.hpp"
#include "common.hpp"
#include "errors.hpp"
#include "histogram_events.hpp"
#include "histogramming.hpp"
#include "introspect.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename ResetEvent, typename OverflowStrategy, typename DataTraits,
          typename Downstream>
class histogram {
  public:
    using bin_index_type = typename DataTraits::bin_index_type;
    using bin_type = typename DataTraits::bin_type;
    static_assert(
        is_any_of_v<OverflowStrategy, saturate_on_overflow, reset_on_overflow,
                    stop_on_overflow, error_on_overflow>);

  private:
    using internal_overflow_strategy = std::conditional_t<
        std::is_same_v<OverflowStrategy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    single_histogram<bin_index_type, bin_type, internal_overflow_strategy>
        shist;
    bool saturated = false;
    Downstream downstream;

    void lazy_start() {
        if (hist_bucket.empty()) { // First time, or after reset.
            hist_bucket = bsource->bucket_of_size(shist.num_bins());
            shist = decltype(shist){hist_bucket, shist};
            shist.clear();
        }
    }

    void emit_concluding() {
        downstream.handle(
            concluding_histogram_event<DataTraits>{std::move(hist_bucket)});
        hist_bucket = {};
    }

    void reset() {
        hist_bucket = {};
        if constexpr (std::is_same_v<OverflowStrategy, saturate_on_overflow>) {
            saturated = false;
        }
    }

    [[noreturn]] void stop() {
        downstream.flush();
        throw end_processing("histogram bin overflowed");
    }

    [[noreturn]] void overflow_error() {
        throw histogram_overflow_error("histogram bin overflowed");
    }

    LIBTCSPC_NOINLINE void
    handle_overflow(bin_increment_event<DataTraits> const &event) {
        if constexpr (std::is_same_v<OverflowStrategy, saturate_on_overflow>) {
            downstream.handle(warning_event{"histogram saturated"});
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            reset_on_overflow>) {
            if (shist.max_per_bin() == 0)
                overflow_error();
            emit_concluding();
            reset();
            handle(event); // Recurse max once
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            stop_on_overflow>) {
            emit_concluding();
            stop();
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            error_on_overflow>) {
            overflow_error();
        } else {
            static_assert(false_for_type<OverflowStrategy>::value);
        }
    }

  public:
    explicit histogram(
        std::size_t num_bins, bin_type max_per_bin,
        std::shared_ptr<bucket_source<bin_type>> buffer_provider,
        Downstream downstream)
        : bsource(std::move(buffer_provider)),
          shist(hist_bucket, arg_max_per_bin{max_per_bin},
                arg_num_bins{num_bins}),
          downstream(std::move(downstream)) {
        if (num_bins == 0)
            throw std::logic_error("histogram must have at least 1 bin");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "histogram");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(bin_increment_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTraits::bin_index_type>);
        lazy_start();
        if (not shist.apply_increments({&event.bin_index, 1})) {
            if constexpr (std::is_same_v<OverflowStrategy,
                                         saturate_on_overflow>) {
                if (not saturated) {
                    saturated = true;
                    handle_overflow(event);
                }
            } else {
                return handle_overflow(event);
            }
        }
        auto const hist_event =
            histogram_event<DataTraits>{hist_bucket.subbucket(0)};
        downstream.handle(hist_event);
    }

    void handle([[maybe_unused]] ResetEvent const &event) {
        lazy_start();
        emit_concluding();
        reset();
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() {
        lazy_start();
        emit_concluding();
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that collects a histogram.
 *
 * \ingroup processors-histogramming
 *
 * The processor builds histograms out of incoming
 * `tcspc::bin_increment_event`s by incrementing the bin at the `bin_index`
 * given by the event. The `abstime` of this event is not used and need not be
 * monotonic. A round of accumulation is ended when a \p ResetEvent is
 * received, upon which accumulation is restarted with an empty histogram.
 *
 * The histogram is stored in a `tcspc::bucket<DataTraits::bin_type>`. Each
 * round of accumulation uses (sequentially) a new bucket from the \p
 * buffer_provider.
 *
 * On every update a `tcspc::histogram_event` is emitted containing a view of
 * the histogram bucket (whose storage is observable but not extractable). At
 * the end of each round of accumulation (i.e., upon a reset), a
 * `tcspc::concluding_histogram_event` is emitted, carrying the histogram
 * bucket (storage can be extracted).
 *
 * \attention Behavior is undefined if an incoming `tcspc::bin_increment_event`
 * contains a bin index beyond the size of the histogram. The bin mapper should
 * be chosen so that this does not occur.
 *
 * \tparam ResetEvent event type causing histogram to reset
 *
 * \tparam OverflowStrategy strategy tag type to select how to handle bin
 * overflows
 *
 * \tparam DataTraits traits type specifying `bin_index_type` and `bin_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param num_bins number of bins in the histogram (must match the bin mapper
 * used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param buffer_provider bucket source providing series of buffers (a new
 * bucket is used after every reset)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bin_increment_event<DT>`: apply the increment to the histogram and
 *   emit (const) `tcspc::histogram_event<DataTraits>`; if a bin overflowed,
 *   behavior (taken before emitting the histogram) depends on
 *   `OverflowStrategy`:
 *   - If `tcspc::saturate_on_overflow`, ignore the event, emitting
 *     `tcspc::warning_event` only on the first overflow since the start or
 *     last reset
 *   - If `tcspc::reset_on_overflow`, behave as if a `ResetEvent` was received
 *     just prior to the current event; then reapply the current event (but
 *     throw `tcspc::histogram_overflow_error` if `max_per_bin` equals 0)
 *   - If `tcspc::stop_on_overflow`, behave as if a `ResetEvent` was received
 *     instead of the current event; then flush the downstream and throw
 *     `tcspc::end_processing`
 *   - If `tcspc::error_on_overflow`, throw `tcspc::histogram_overflow_error`
 * - `ResetEvent`: emit (rvalue)
 *   `tcspc::concluding_histogram_event<DataTraits>` with the current
 *   histogram; then clear the histogram and other state
 * - All other types: pass through with no action
 * - Flush: emit (rvalue) `tcspc::concluding_histogram_event<DataTraits>` with
 *   the current histogram; pass through
 */
template <typename ResetEvent, typename OverflowStrategy,
          typename DataTraits = default_data_traits, typename Downstream>
auto histogram(std::size_t num_bins, typename DataTraits::bin_type max_per_bin,
               std::shared_ptr<bucket_source<typename DataTraits::bin_type>>
                   buffer_provider,
               Downstream &&downstream) {
    return internal::histogram<ResetEvent, OverflowStrategy, DataTraits,
                               Downstream>(
        num_bins, max_per_bin, std::move(buffer_provider),
        std ::forward<Downstream>(downstream));
}

} // namespace tcspc
