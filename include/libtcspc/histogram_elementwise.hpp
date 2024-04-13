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

template <typename OverflowStrategy, typename DataTraits, typename Downstream>
class histogram_elementwise {
  public:
    using bin_index_type = typename DataTraits::bin_index_type;
    using bin_type = typename DataTraits::bin_type;
    static_assert(is_any_of_v<OverflowStrategy, saturate_on_overflow,
                              error_on_overflow>);

  private:
    using internal_overflow_strategy = std::conditional_t<
        std::is_same_v<OverflowStrategy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    multi_histogram<bin_index_type, bin_type, internal_overflow_strategy>
        mhist;
    bool saturated = false;
    null_journal<bin_index_type> journal; // Journaling not required
    Downstream downstream;

    LIBTCSPC_NOINLINE void handle_overflow() {
        if constexpr (std::is_same_v<OverflowStrategy, saturate_on_overflow>) {
            downstream.handle(warning_event{"histogram array saturated"});
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            error_on_overflow>) {
            throw histogram_overflow_error("histogram array bin overflowed");
        } else {
            static_assert(false_for_type<OverflowStrategy>::value);
        }
    }

  public:
    explicit histogram_elementwise(
        std::size_t num_elements, std::size_t num_bins, bin_type max_per_bin,
        std::shared_ptr<bucket_source<bin_type>> bucket_source,
        Downstream downstream)
        : bsource(std::move(bucket_source)),
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
            if constexpr (std::is_same_v<OverflowStrategy,
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
            element_histogram_event<DataTraits>{hist_bucket.subbucket(
                element_index * mhist.num_bins(), mhist.num_bins())};
        downstream.handle(elem_event);

        if (mhist.is_complete()) {
            downstream.handle(
                histogram_array_event<DataTraits>{std::move(hist_bucket)});
            hist_bucket = {};
            if constexpr (std::is_same_v<OverflowStrategy,
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

// Design notes:
//
// 1. The non-accumulating histogram_elementwise does not provide a "reset"
// input. It is not clear if this is ever needed, so leaving it out for now.
//
// 2. If the last cycle is left incomplete, no event is emitted containing the
// incomplete array of histograms. Such an event would only be useful for
// progressive display, but progressive display should (necessarily) rely on
// `element_histogram_event`.

/**
 * \brief Create a processor that computes an array of histograms over cycles
 * of batches of datapoints.
 *
 * \ingroup processors-histogram
 *
 * This is the array version of histogram_in_batches (rather than histogram).
 *
 * Each incoming bin_increment_batch_event is stored as successive elements in
 * an array of histograms. On each batch, an element_histogram_event is
 * emitted, referencing the corresponding (single) histogram.
 *
 * At the end of each cycle a histogram_array_event is emitted, referencing the
 * whole array of histograms from the cycle.
 *
 * A \c warning_event is emitted if \c OverflowStrategy is \c
 * saturate_on_overflow and a saturation occurred for the first time in the
 * current cycle.
 *
 * The input events are not required to be in correct abstime order; the event
 * abstime is not used.
 *
 * \tparam OverflowStrategy strategy tag type to select how to handle bin
 * overflows
 *
 * \tparam DataTraits traits type specifying \c bin_index_type, and \c bin_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param bucket_source bucket source providing series of buffers for each
 * histogram array
 *
 * \param downstream downstream processor (moved out)
 *
 * \return histogram-array processor
 */
template <typename OverflowStrategy, typename DataTraits = default_data_traits,
          typename Downstream>
auto histogram_elementwise(
    std::size_t num_elements, std::size_t num_bins,
    typename DataTraits::bin_type max_per_bin,
    std::shared_ptr<bucket_source<typename DataTraits::bin_type>>
        bucket_source,
    Downstream &&downstream) {
    return internal::histogram_elementwise<OverflowStrategy, DataTraits,
                                           Downstream>(
        num_elements, num_bins, max_per_bin, std::move(bucket_source),
        std::forward<Downstream>(downstream));
}

namespace internal {

template <typename ResetEvent, typename OverflowStrategy, bool EmitConcluding,
          typename DataTraits, typename Downstream>
class histogram_elementwise_accumulate {
  public:
    using bin_index_type = typename DataTraits::bin_index_type;
    using bin_type = typename DataTraits::bin_type;

    static_assert(
        is_any_of_v<OverflowStrategy, saturate_on_overflow, reset_on_overflow,
                    stop_on_overflow, error_on_overflow>);

    // EmitConcluding cannot be used with saturate-on-overflow because there is
    // no way to roll back the current cycle in the presense of lost counts due
    // to saturation.
    static_assert(not(EmitConcluding &&
                      std::is_same_v<OverflowStrategy, saturate_on_overflow>),
                  "EmitConcluding is incompatible with saturate_on_overflow");

    // We require EmitConcluding for reset/stop-on-overflow, because it doesn't
    // make much sense to use those overflow policies without the
    // cycle-atomic concluding array event. I don't want to increase unit test
    // code to test such cases, so disallow.
    static_assert(EmitConcluding ||
                      not is_any_of_v<reset_on_overflow, stop_on_overflow>,
                  "EmitConcluding must be true for this overflow policy");

  private:
    using internal_overflow_strategy = std::conditional_t<
        std::is_same_v<OverflowStrategy, saturate_on_overflow>,
        saturate_on_internal_overflow, stop_on_internal_overflow>;
    static constexpr bool need_journal =
        EmitConcluding || std::is_same_v<OverflowStrategy, reset_on_overflow>;
    using journal_type =
        std::conditional_t<need_journal,
                           bin_increment_batch_journal<bin_index_type>,
                           null_journal<bin_index_type>>;

    std::shared_ptr<bucket_source<bin_type>> bsource;
    bucket<bin_type> hist_bucket;
    multi_histogram_accumulation<bin_index_type, bin_type,
                                 internal_overflow_strategy>
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
        if constexpr (std::is_same_v<OverflowStrategy, saturate_on_overflow>) {
            downstream.handle(warning_event{"histogram array saturated"});
        } else if constexpr (std::is_same_v<OverflowStrategy,
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
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            stop_on_overflow>) {
            if constexpr (EmitConcluding) {
                mhista.roll_back_current_cycle(journal);
                emit_concluding();
            }
            stop();
        } else if constexpr (std::is_same_v<OverflowStrategy,
                                            error_on_overflow>) {
            overflow_error("histogram array bin overflowed");
        } else {
            static_assert(false_for_type<OverflowStrategy>::value);
        }
    }

  public:
    explicit histogram_elementwise_accumulate(
        std::size_t num_elements, std::size_t num_bins, bin_type max_per_bin,
        std::shared_ptr<bucket_source<bin_type>> bucket_source,
        Downstream downstream)
        : bsource(std::move(bucket_source)),
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

        auto const elem_event =
            element_histogram_event<DataTraits>{hist_bucket.subbucket(
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
        if constexpr (std::is_same_v<OverflowStrategy, saturate_on_overflow>) {
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
 * \brief Create a processor that collects an array of histograms accumulated
 * over cycles of batches of datapoints.
 *
 * \ingroup processors-histogram
 *
 * The reset event \c ResetEvent causes the array of histograms to be cleared
 * and a new accumulation to be started.
 *
 * A \c warning_event is emitted if \c OverflowStrategy is \c
 * saturate_on_overflow and a saturation occurred for the first time since the
 * last reset (or start of stream).
 *
 * \tparam ResetEvent type of event causing histograms to reset
 *
 * \tparam OverflowStrategy strategy tag type to select how to handle bin
 * overflows
 *
 * \tparam EmitConcluding if true, emit a \c concluding_histogram_array_event
 * each time a cycle completes
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c
 * bin_index_type, and \c bin_type
 *
 * \tparam Downstream downstream processor type
 *
 * \param num_elements the number of elements (each a histogram) in the array
 *
 * \param num_bins the number of bins in each histogram (must match the bin
 * mapper used upstream)
 *
 * \param max_per_bin maximum value allowed in each bin
 *
 * \param bucket_source bucket source providing series of buffers (a new one is
 * used after each reset)
 *
 * \param downstream downstream processor (moved out)
 *
 * \return accumulate-histogram-arrays processor
 */
template <typename ResetEvent, typename OverflowStrategy,
          bool EmitConcluding = false,
          typename DataTraits = default_data_traits, typename Downstream>
auto histogram_elementwise_accumulate(
    std::size_t num_elements, std::size_t num_bins,
    typename DataTraits::bin_type max_per_bin,
    std::shared_ptr<bucket_source<typename DataTraits::bin_type>>
        bucket_source,
    Downstream &&downstream) {
    return internal::histogram_elementwise_accumulate<
        ResetEvent, OverflowStrategy, EmitConcluding, DataTraits, Downstream>(
        num_elements, num_bins, max_per_bin, std::move(bucket_source),
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
