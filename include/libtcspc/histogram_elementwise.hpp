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

#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename OverflowPolicy, typename DataTraits, typename Downstream>
class histogram_elementwise {
  public:
    using bin_index_type = typename DataTraits::bin_index_type;
    using bin_type = typename DataTraits::bin_type;
    static_assert(
        is_any_of_v<OverflowPolicy, saturate_on_overflow, error_on_overflow>);

  private:
    using internal_overflow_policy = std::conditional_t<
        std::is_same_v<OverflowPolicy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    multi_histogram<bin_index_type, bin_type, internal_overflow_policy> mhist;
    bool saturated = false;
    null_journal<bin_index_type> journal; // Journaling not required
    Downstream downstream;

    LIBTCSPC_NOINLINE void handle_overflow() {
        if constexpr (std::is_same_v<OverflowPolicy, saturate_on_overflow>) {
            downstream.handle(warning_event{"histogram array saturated"});
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            error_on_overflow>) {
            throw histogram_overflow_error("histogram array bin overflowed");
        } else {
            static_assert(false_for_type<OverflowPolicy>::value);
        }
    }

  public:
    explicit histogram_elementwise(
        std::size_t num_elements, std::size_t num_bins, bin_type max_per_bin,
        std::shared_ptr<bucket_source<bin_type>> buffer_provider,
        Downstream downstream)
        : bsource(std::move(buffer_provider)),
          mhist(hist_bucket, arg_max_per_bin{max_per_bin},
                arg_num_bins{num_bins}, arg_num_elements{num_elements}, true),
          downstream(std::move(downstream)) {
        if (num_elements == 0)
            throw std::logic_error(
                "histogram_elementsiwe must have at least 1 element");
        if (num_bins == 0)
            throw std::logic_error(
                "histogram_elementsiwe must have at least 1 bin per element");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "histogram_elementwise");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT>
    void handle(bin_increment_batch_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTraits::bin_index_type>);
        if (hist_bucket.empty()) { // First time, or after completion.
            hist_bucket = bsource->bucket_of_size(mhist.num_elements() *
                                                  mhist.num_bins());
            mhist = decltype(mhist){hist_bucket, mhist, true};
        }
        assert(not mhist.is_complete());
        auto element_index = mhist.next_element_index();
        if (not mhist.apply_increment_batch(event.bin_indices, journal)) {
            if constexpr (std::is_same_v<OverflowPolicy,
                                         saturate_on_overflow>) {
                if (not saturated) {
                    saturated = true;
                    handle_overflow();
                }
            } else {
                return handle_overflow();
            }
        }

        auto const elem_event =
            histogram_event<DataTraits>{hist_bucket.subbucket(
                element_index * mhist.num_bins(), mhist.num_bins())};
        downstream.handle(elem_event);

        if (mhist.is_complete()) {
            downstream.handle(
                histogram_array_event<DataTraits>{std::move(hist_bucket)});
            hist_bucket = {};
            if constexpr (std::is_same_v<OverflowPolicy,
                                         saturate_on_overflow>) {
                saturated = false;
            }
        }
    }

    template <typename Event> void handle(Event const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collects time-divided arrays of histograms.
 *
 * \ingroup processors-histogramming
 *
 * The processor builds an array of histograms sequentially, computing each
 * element histogram from an incoming `tcspc::bin_increment_batch_event`. When
 * it has finished filling the histogram array, it starts over with a new
 * array.
 *
 * The histogram array is stored in a `tcspc::bucket<DataTraits::bin_type>` of
 * size `num_elements * num_bins`. Each cycle (of `num_elements` bin increment
 * batches) uses (sequentially) a new bucket from the \p buffer_provider.
 *
 * On every bin increment batch received a `tcspc::histogram_event` is
 * emitted containing the corresponding subview of the histogram array bucket
 * (whose storage is observable but not extractable). At the end of each cycle,
 * a `tcspc::histogram_array_event` is emitted, carrying the histogram array
 * bucket (storage can be extracted).
 *
 * \attention Behavior is undefined if an incoming
 * `tcspc::bin_increment_batch_event` contains a bin index beyond the size of
 * the histogram. The bin mapper should be chosen so that this does not occur.
 *
 * \tparam OverflowPolicy policy tag type to select how to handle bin
 * overflows
 *
 * \tparam DataTraits traits type specifying `bin_index_type` and `bin_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param buffer_provider bucket source providing series of buffers for each
 * histogram array
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bin_increment_batch_event<DT>`: apply the increments to the next
 *   element histogram of the array and emit (const)
 *   `tcspc::histogram_event<DataTraits>`; if the batch filled the last
 *   element of the array, emit (rvalue)
 *   `tcspc::histogram_array_event<DataTraits>`; if a bin overflowed, behavior
 *   (taken before emitting the above events) depends on `OverflowPolicy`:
 *   - If `tcspc::saturate_on_overflow`, ignore the increment, emitting
 *     `tcspc::warning_event` only on the first overflow of the cycle
 *   - If `tcspc::error_on_overflow`, throw `tcspc::histogram_overflow_error`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename OverflowPolicy, typename DataTraits = default_data_traits,
          typename Downstream>
auto histogram_elementwise(
    std::size_t num_elements, std::size_t num_bins,
    typename DataTraits::bin_type max_per_bin,
    std::shared_ptr<bucket_source<typename DataTraits::bin_type>>
        buffer_provider,
    Downstream &&downstream) {
    return internal::histogram_elementwise<OverflowPolicy, DataTraits,
                                           Downstream>(
        num_elements, num_bins, max_per_bin, std::move(buffer_provider),
        std::forward<Downstream>(downstream));
}

namespace internal {

template <typename ResetEvent, typename OverflowPolicy, bool EmitConcluding,
          typename DataTraits, typename Downstream>
class histogram_elementwise_accumulate {
  public:
    using bin_index_type = typename DataTraits::bin_index_type;
    using bin_type = typename DataTraits::bin_type;

    static_assert(
        is_any_of_v<OverflowPolicy, saturate_on_overflow, reset_on_overflow,
                    stop_on_overflow, error_on_overflow>);

    // EmitConcluding cannot be used with saturate-on-overflow because there is
    // no way to roll back the current cycle in the presense of lost counts due
    // to saturation.
    static_assert(not(EmitConcluding &&
                      std::is_same_v<OverflowPolicy, saturate_on_overflow>),
                  "EmitConcluding is incompatible with saturate_on_overflow");

    // We require EmitConcluding for reset/stop-on-overflow, because it doesn't
    // make much sense to use those overflow policies without the
    // cycle-atomic concluding array event. I don't want to increase unit test
    // code to test such cases, so disallow.
    static_assert(EmitConcluding ||
                      not is_any_of_v<reset_on_overflow, stop_on_overflow>,
                  "EmitConcluding must be true for this overflow policy");

  private:
    using internal_overflow_policy = std::conditional_t<
        std::is_same_v<OverflowPolicy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;
    static constexpr bool need_journal =
        EmitConcluding || std::is_same_v<OverflowPolicy, reset_on_overflow>;
    using journal_type =
        std::conditional_t<need_journal,
                           bin_increment_batch_journal<bin_index_type>,
                           null_journal<bin_index_type>>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    multi_histogram_accumulation<bin_index_type, bin_type,
                                 internal_overflow_policy>
        mhista;
    bool saturated = false;
    journal_type journal;
    Downstream downstream;

    void lazy_start() {
        if (hist_bucket.empty()) { // First time, or after reset.
            hist_bucket = bsource->bucket_of_size(mhista.num_elements() *
                                                  mhista.num_bins());
            mhista = decltype(mhista){hist_bucket, mhista, true};
        }
    }

    void emit_concluding() {
        assert(mhista.is_consistent());
        downstream.handle(concluding_histogram_array_event<DataTraits>{
            std::move(hist_bucket)});
        hist_bucket = {};
    }

    [[noreturn]] void stop() {
        downstream.flush();
        throw end_processing("histogram array bin overflowed");
    }

    [[noreturn]] void overflow_error(char const *msg) {
        throw histogram_overflow_error(msg);
    }

    LIBTCSPC_NOINLINE void
    handle_overflow(bin_increment_batch_event<DataTraits> const &event) {
        if constexpr (std::is_same_v<OverflowPolicy, saturate_on_overflow>) {
            downstream.handle(warning_event{"histogram array saturated"});
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            reset_on_overflow>) {
            if (mhista.cycle_index() == 0) {
                overflow_error(
                    "histogram array bin overflowed on a single batch");
            }
            mhista.roll_back_current_cycle(journal);
            if constexpr (EmitConcluding)
                emit_concluding();
            hist_bucket = bsource->bucket_of_size(mhista.num_elements() *
                                                  mhista.num_bins());
            mhista = decltype(mhista){hist_bucket, mhista, true};
            mhista.replay(journal);
            return handle(event); // Recurse max once
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            stop_on_overflow>) {
            if constexpr (EmitConcluding) {
                mhista.roll_back_current_cycle(journal);
                emit_concluding();
            }
            stop();
        } else if constexpr (std::is_same_v<OverflowPolicy,
                                            error_on_overflow>) {
            overflow_error("histogram array bin overflowed");
        } else {
            static_assert(false_for_type<OverflowPolicy>::value);
        }
    }

  public:
    explicit histogram_elementwise_accumulate(
        std::size_t num_elements, std::size_t num_bins, bin_type max_per_bin,
        std::shared_ptr<bucket_source<bin_type>> buffer_provider,
        Downstream downstream)
        : bsource(std::move(buffer_provider)),
          mhista(hist_bucket, arg_max_per_bin{max_per_bin},
                 arg_num_bins{num_bins}, arg_num_elements{num_elements}, true),
          downstream(std::move(downstream)) {
        if (num_elements == 0)
            throw std::logic_error(
                "histogram_elementsiwe_accumulate must have at least 1 element");
        if (num_bins == 0)
            throw std::logic_error(
                "histogram_elementsiwe_accumulate must have at least 1 bin per element");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "histogram_elementwise_accumulate");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT>
    void handle(bin_increment_batch_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTraits::bin_index_type>);
        lazy_start();
        assert(not mhista.is_cycle_complete());
        auto element_index = mhista.next_element_index();
        if (not mhista.apply_increment_batch(event.bin_indices, journal)) {
            if constexpr (std::is_same_v<OverflowPolicy,
                                         saturate_on_overflow>) {
                if (not saturated) {
                    saturated = true;
                    handle_overflow(event);
                }
            } else {
                return handle_overflow(event);
            }
        }

        auto const elem_event =
            histogram_event<DataTraits>{hist_bucket.subbucket(
                element_index * mhista.num_bins(), mhista.num_bins())};
        downstream.handle(elem_event);

        if (mhista.is_cycle_complete()) {
            mhista.new_cycle(journal);
            auto const array_event =
                histogram_array_event<DataTraits>{hist_bucket.subbucket(0)};
            downstream.handle(array_event);
        }
    }

    void handle([[maybe_unused]] ResetEvent const &event) {
        if constexpr (EmitConcluding) {
            lazy_start();
            mhista.roll_back_current_cycle(journal);
            emit_concluding();
        }
        hist_bucket = {};
        if constexpr (std::is_same_v<OverflowPolicy, saturate_on_overflow>) {
            saturated = false;
        }
        journal.clear();
    }

    template <typename Event> void handle(Event const &event) {
        downstream.handle(event);
    }

    void flush() {
        if constexpr (EmitConcluding) {
            lazy_start();
            mhista.roll_back_current_cycle(journal);
            emit_concluding();
        }
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that collects time-divided arrays of histograms
 * accumulated over multiple cycles.
 *
 * \ingroup processors-histogramming
 *
 * The processor builds an array of histograms sequentially, incrementing bins
 * in each element histogram based on incoming
 * `tcspc::bin_increment_batch_event`s. When it has finished updating all
 * elements of the histogram array, it returns to the beginning of the array
 * and continues to accumulate counts. A round of accumulation is ended when a
 * \p ResetEvent is received, upon which accumulation is restarted with an
 * empty histogram array.
 *
 * The histogram array is stored in a `tcspc::bucket<DataTraits::bin_type>` of
 * size `num_elements * num_bins`. Each round of accumulation uses
 * (sequentially) a new bucket from the \p buffer_provider.
 *
 * On every bin increment batch received a `tcspc::histogram_event` is
 * emitted containing the corresponding subview of the histogram array bucket
 * (whose storage is observable but not extractable). At the end of each cycle
 * through the array, a `tcspc::histogram_array_event` is emitted, containing a
 * view of the whole histogram array bucket (again, with observable but
 * non-extractable storage).
 *
 * At the end of each round of accumulation (i.e., upon a reset), any
 * incomplete cycle through the array is rolled back and a
 * `tcspc::concluding_histogram_array_event` is emitted, carrying the histogram
 * array bucket (storage can be extracted).
 *
 * \attention Behavior is undefined if an incoming `tcspc::bin_increment_event`
 * contains a bin index beyond the size of the histogram. The bin mapper should
 * be chosen so that this does not occur.
 *
 * \tparam ResetEvent type of event causing histograms to reset
 *
 * \tparam OverflowPolicy policy tag type to select how to handle bin
 * overflows
 *
 * \tparam EmitConcluding if true, emit a
 * `tcspc::concluding_histogram_array_event` each time a round of accumulation
 * completes
 *
 * \tparam DataTraits traits type specifying `bin_index_type` and `bin_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param buffer_provider bucket source providing series of buffers (a new one
 * is used after each reset)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bin_increment_batch_event<DT>`: apply the increments to the next
 *   element histogram of the array and emit (const)
 *   `tcspc::histogram_event<DataTraits>`; if the batch updated the
 *   last element of the array, emit (rvalue)
 *   `tcspc::histogram_array_event<DataTraits>`; if a bin overflowed, behavior
 *   (taken before emitting the above events) depends on `OverflowPolicy`:
 *   - If `tcspc::saturate_on_overflow`, ignore the increment, emitting
 *     `tcspc::warning_event` only on the first overflow of the current round
 *     of accumulation
 *   - If `tcspc::reset_on_overflow`, behave as if a `ResetEvent` was received
 *     just prior to the current event; then replay any partial cycle that was
 *     rolled back during the reset and reapply the current event (but throw
 *     `tcspc::histogram_overflow_error` if this causes an overflow by itself)
 *   - If `tcspc::stop_on_overflow`, behave as if a `ResetEvent` was received
 *     instead of the current event; then flush the downstream and throw
 *     `tcspc::end_processing`
 *   - If `tcspc::error_on_overflow`, throw `tcspc::histogram_overflow_error`
 * - `ResetEvent`: if `EmitConcluding` is true, emit (rvalue)
 *   `tcspc::concluding_histogram_array_event<DataTraits>` with the current
 *   histogram array (after rolling back any partial cycle); then clear the
 *   array and other state
 * - All other types: pass through with no action
 * - Flush: if `EmitConcluding` is true, emit (rvalue)
 *   `tcspc::concluding_histogram_array_event<DataTraits>` with the current
 *   histogram array (after rolling back any partial cycle); pass through
 */
template <typename ResetEvent, typename OverflowPolicy,
          bool EmitConcluding = false,
          typename DataTraits = default_data_traits, typename Downstream>
auto histogram_elementwise_accumulate(
    std::size_t num_elements, std::size_t num_bins,
    typename DataTraits::bin_type max_per_bin,
    std::shared_ptr<bucket_source<typename DataTraits::bin_type>>
        buffer_provider,
    Downstream &&downstream) {
    return internal::histogram_elementwise_accumulate<
        ResetEvent, OverflowPolicy, EmitConcluding, DataTraits, Downstream>(
        num_elements, num_bins, max_per_bin, std::move(buffer_provider),
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
