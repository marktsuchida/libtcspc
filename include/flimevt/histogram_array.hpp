/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "binning.hpp"
#include "common.hpp"
#include "histogram_events.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <exception>
#include <iterator>
#include <vector>

namespace flimevt {

namespace internal {

template <typename TBinIndex, typename EStart, typename D>
class journal_bin_increment_batches {
    bool started = false;
    bin_increment_batch_journal_event<TBinIndex> je;

    std::size_t const batches_per_cycle;

    D downstream;

  public:
    explicit journal_bin_increment_batches(std::size_t batches_per_cycle,
                                           D &&downstream)
        : batches_per_cycle(batches_per_cycle), downstream(downstream) {
        assert(batches_per_cycle > 0);
    }

    void
    handle_event(bin_increment_batch_event<TBinIndex> const &event) noexcept {
        if (!started)
            return;

        if (je.journal.num_batches() == 0)
            je.start = event.start;

        je.journal.append_batch(event.bin_indices);
        downstream.handle_event(event);

        if (je.journal.num_batches() == batches_per_cycle) {
            je.stop = event.stop;
            downstream.handle_event(je);
            started = false;
        }
    }

    void handle_event(EStart const &event) noexcept {
        if (started) {
            partial_bin_increment_batch_journal_event<TBinIndex> e{
                je.start, je.stop, {}};
            e.journal.swap(je.journal);
            downstream.handle_event(e);
            je.journal.swap(e.journal);
        }
        started = true;
        je.start = je.stop = 0;
        je.journal.clear();
        downstream.handle_event(event);
    }

    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (started) {
            partial_bin_increment_batch_journal_event<TBinIndex> e{
                je.start, je.stop, {}};
            e.journal.swap(je.journal);
            downstream.handle_event(e);
            je.journal.swap(e.journal);
        }
        je.journal.clear_and_shrink_to_fit();
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * Create a processor that journals bin increment batches.
 *
 * This is used in conjunction with accumulate_histogram_arrays to enable
 * rolling back partial frames. It can also be used with histogram_array or
 * accumulate_histogram_array when set to process whole frames at a time.
 *
 * The journal stores the bin increments for each batch in a cycle, whose size
 * is determined by \c batches_per_cycle. A cycle starts when an \c EStart
 * event is received (passed through) and ends before the next \c EStart or
 * end-of-stream. The journal is cleared at the start of each cycle. Within
 * each cycle, events of type bin_increment_batch_event (also passed through)
 * are collected into the journal. When \c batches_per_cycle of them have been
 * received, a bin_increment_batch_journal_event is emitted, containing the
 * journal for the cycle.
 *
 * If the next \c EStart event is received before \c batches_per_cycle batches
 * have been journaled, a partial_bin_increment_batch_journal_event is emitted,
 * containing the batches journaled so far.
 *
 * Any bin_increment_batch_event before the first \c EStart or exceeding \c
 * batches_per_cycle is ignored and not passed through.
 *
 * \tparam TBinIndex histogram bin index type
 * \tparam EStart event type to start a new cycle
 * \tparam D downstream processor type
 * \param batches_per_cycle number of batches per cycle
 * \param downstream downstream processor (moved out)
 * \return journal-bin-increment-batches processor
 */
template <typename TBinIndex, typename EStart, typename D>
auto journal_bin_increment_batches(std::size_t batches_per_cycle,
                                   D &&downstream) {
    return internal::journal_bin_increment_batches<TBinIndex, EStart, D>(
        batches_per_cycle, std::forward<D>(downstream));
}

} // namespace flimevt
