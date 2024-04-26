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

template <typename ResetEvent, typename OverflowPolicy, typename DataTypes,
          typename Downstream>
class histogram {
  public:
    using bin_index_type = typename DataTypes::bin_index_type;
    using bin_type = typename DataTypes::bin_type;
    static_assert(is_any_of_v<OverflowPolicy, saturate_on_overflow_t,
                              reset_on_overflow_t, stop_on_overflow_t,
                              error_on_overflow_t>);

  private:
    using internal_overflow_policy = std::conditional_t<
        std::is_same_v<OverflowPolicy, saturate_on_overflow_t>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    single_histogram<bin_index_type, bin_type, internal_overflow_policy> shist;
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
            concluding_histogram_event<DataTypes>{std::move(hist_bucket)});
        hist_bucket = {};
    }

    void reset() {
        hist_bucket = {};
        if constexpr (std::is_same_v<OverflowPolicy, saturate_on_overflow_t>) {
            saturated = false;
        }
    }

    [[noreturn]] void stop() {
        downstream.flush();
        throw end_of_processing("histogram bin overflowed");
    }

    [[noreturn]] void overflow_error() {
        throw histogram_overflow_error("histogram bin overflowed");
    }

    LIBTCSPC_NOINLINE void
    handle_overflow(bin_increment_event<DataTypes> const &event) {
        if constexpr (std::is_same_v<OverflowPolicy, saturate_on_overflow_t>) {
            downstream.handle(warning_event{"histogram saturated"});
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            reset_on_overflow_t>) {
            if (shist.max_per_bin() == 0)
                overflow_error();
            emit_concluding();
            reset();
            handle(event); // Recurse max once
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            stop_on_overflow_t>) {
            emit_concluding();
            stop();
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            error_on_overflow_t>) {
            overflow_error();
        } else {
            static_assert(false_for_type<OverflowPolicy>::value);
        }
    }

  public:
    explicit histogram(
        arg::num_bins<std::size_t> num_bins,
        arg::max_per_bin<bin_type> max_per_bin,
        std::shared_ptr<bucket_source<bin_type>> buffer_provider,
        Downstream downstream)
        : bsource(std::move(buffer_provider)),
          shist(hist_bucket, max_per_bin, num_bins),
          downstream(std::move(downstream)) {
        if (num_bins.value == 0)
            throw std::invalid_argument("histogram must have at least 1 bin");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "histogram");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT> void handle(bin_increment_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        lazy_start();
        if (not shist.apply_increments({&event.bin_index, 1})) {
            if constexpr (std::is_same_v<OverflowPolicy,
                                         saturate_on_overflow_t>) {
                if (not saturated) {
                    saturated = true;
                    handle_overflow(event);
                }
            } else {
                return handle_overflow(event);
            }
        }
        auto const hist_event =
            histogram_event<DataTypes>{hist_bucket.subbucket(0)};
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

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collects a histogram.
 *
 * \ingroup processors-histogramming
 *
 * The processor builds histograms out of incoming
 * `tcspc::bin_increment_event`s by incrementing the bin at the `bin_index`
 * given by the event. A round of accumulation is ended when a \p ResetEvent is
 * received, upon which accumulation is restarted with an empty histogram.
 *
 * The histogram is stored in a `tcspc::bucket<DataTypes::bin_type>`. Each
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
 * \tparam DataTypes data type set specifying `bin_index_type` and `bin_type`
 *
 * \tparam OverflowPolicy policy tag type (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param policy policy tag instance to select how to handle bin overflows
 * (`tcspc::saturate_on_overflow`, `tcspc::reset_on_overflow`,
 * `tcspc::stop_on_overflow`, or `tcspc::error_on_overflow`)
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
 *   emit (const) `tcspc::histogram_event<DataTypes>`; if a bin overflowed,
 *   behavior (taken before emitting the histogram) depends on
 *   `OverflowPolicy`:
 *   - If `tcspc::saturate_on_overflow_t`, ignore the event, emitting
 *     `tcspc::warning_event` only on the first overflow since the start or
 *     last reset
 *   - If `tcspc::reset_on_overflow_t`, behave as if a `ResetEvent` was
 *     received just prior to the current event; then reapply the current event
 *     (but throw `tcspc::histogram_overflow_error` if `max_per_bin` equals 0)
 *   - If `tcspc::stop_on_overflow_t`, behave as if a `ResetEvent` was received
 *     instead of the current event; then flush the downstream and throw
 *     `tcspc::end_of_processing`
 *   - If `tcspc::error_on_overflow_t`, throw `tcspc::histogram_overflow_error`
 * - `ResetEvent`: emit (rvalue)
 *   `tcspc::concluding_histogram_event<DataTypes>` with the current
 *   histogram; then clear the histogram and other state
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename ResetEvent, typename DataTypes = default_data_types,
          typename OverflowPolicy, typename Downstream>
auto histogram([[maybe_unused]] OverflowPolicy const &policy,
               arg::num_bins<std::size_t> num_bins,
               arg::max_per_bin<typename DataTypes::bin_type> max_per_bin,
               std::shared_ptr<bucket_source<typename DataTypes::bin_type>>
                   buffer_provider,
               Downstream &&downstream) {
    return internal::histogram<ResetEvent, OverflowPolicy, DataTypes,
                               Downstream>(
        num_bins, max_per_bin, std::move(buffer_provider),
        std ::forward<Downstream>(downstream));
}

} // namespace tcspc
