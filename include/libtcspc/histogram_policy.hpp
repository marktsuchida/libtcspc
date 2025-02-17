/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <type_traits>

namespace tcspc {

/**
 * \brief Histogramming policy specifying behavor.
 *
 * \ingroup histogram-policy
 *
 * This is a bit-flag enum type. Operators `|`, `&`, `~`, `|=`, and `&=` (not
 * shown here) are defined on values of this type.
 *
 * The policy consists of a choice of behavior on bin overflow, plus a number
 * of flags defining behavior, some of which only apply to
 * `tcspc::scan_histograms()` (and have no effect on `tcspc::histogram()`).
 *
 * Only one overflow behavior value may be used at a time; combining more than
 * one via the `|` operator results in an unexpected value. All other flags
 * (and `default_policy`) may be combined in any way.
 */
enum class histogram_policy : unsigned {
    /**
     * \brief Default policy with no bit set: equal to `error_on_overflow`.
     */
    default_policy = 0,

    /**
     * \brief Treat a histogram bin overflow as an error.
     *
     * If an increment is about to cause a bin overflow, throw
     * `tcspc::histogram_overflow_error`.
     *
     * This is the default overflow behavior if none is given.
     */
    error_on_overflow = 0b00,

    /**
     * \brief Treat a histogram bin overflow as end of processing.
     *
     * If an increment is about to cause a bin overflow, perform a reset, flush
     * the downstream, and throw `tcspc::end_of_processing`.
     *
     * This is almost always used together with `emit_concluding_events`.
     */
    stop_on_overflow = 0b01,

    /**
     * \brief Ignore increments that would cause a bin overflow.
     *
     * On the first overflow since the last reset (or start), emit a
     * `tcspc::warning_event`.
     */
    saturate_on_overflow = 0b10,

    /**
     * \brief Perform a reset when a histogram bin is about to overflow.
     *
     * The increment that would have triggered the bin overflow is applied
     * after the reset.
     *
     * In the case of `tcspc::scan_histograms()`, the partial scan that is
     * rolled back during the reset is reapplied after the reset, such that no
     * counts are lost.
     *
     * This is almost always used together with `emit_concluding_events`.
     *
     * This behavior can be used to collect a series of histograms with bins of
     * low bit width without losing any counts by overflow; the histograms can
     * later be added up (using wider bins) to get the total.
     */
    reset_on_overflow = 0b11,

    /**
     * \brief Bitmask for overflow behavior.
     *
     * The bitwise AND (`&`) with any `histogram_policy` value gives one of
     * `saturate_on_overflow`, `reset_on_overflow`, `stop_on_overflow`, or
     * `error_on_overflow`.
     */
    overflow_mask = 0b11,

    /**
     * \brief Enable generation of `tcspc::concluding_histogram_array_event`.
     *
     * If set for `tcspc::histogram()`, emit
     * `tcspc::concluding_histogram_event` upon every reset.
     *
     * If set for `tcspc::scan_histograms()`, emit
     * `tcspc::concluding_histogram_array_event` upon every reset. The
     * concluding event contains the accumulated histogram array with any
     * partial scan having been rolled back.
     *
     * This setting is provided as an option so that the overhead of rolling
     * back partial scans can be avoided when the result is not used.
     *
     * This flag is not supported in combination with `saturate_on_overflow` in
     * the case of `tcspc::scan_histograms()` (because there is no way to roll
     * back a partial scan under `saturate_on_overflow`).
     */
    emit_concluding_events = 1 << 2,

    /**
     * \brief Automatically reset when the end of a scan has been reached.
     *
     * Applies to `tcspc::scan_histograms()`. If set, perform a reset after
     * each `tcspc::histogram_array_event` is emitted.
     *
     * This is one way to disable accumulation of multiple scans. Typically
     * used with `error_on_overflow` or `saturate_on_overflow`, as well as
     * `emit_concluding_events`, this setting causes the histogram array
     * resulting from each scan to be emitted as a new bucket (in a
     * `tcspc::concluding_histogram_array_event`)---which is often desirable if
     * saving the histogram array from each scan.
     */
    reset_after_scan = 1 << 3,

    /**
     * \brief Clear element histograms before applying bin increment batches,
     * during every scan.
     *
     * Applies to `tcspc::scan_histograms()`. If set, overwrite each element
     * histogram with the counts from the current scan.
     *
     * This is another way to disable accumulation of multiple scans. Typically
     * used with `error_on_overflow` or `saturate_on_overflow`, this setting
     * causes the histogram array to be overwritten on a per-element basis,
     * instead of adding to the histogram from previous scans. In each emitted
     * `tcspc::histogram_array_progress_event` the unfinished portion of the
     * current scan contains the data from the previous scan---which may be
     * reasonable behavior for a live display.
     */
    clear_every_scan = 1 << 4,

    /**
     * \brief Do not zero-fill the histogram array at the beginning of a round
     * of accumulation.
     *
     * Applies to `tcspc::scan_histograms()`. If set, the unfilled portion of
     * the histogram array (observable via
     * `tcspc::histogram_array_progress_event`s during the first scan of each
     * round of accumulation) is left uninitialized. Even with this setting,
     * each element histogram is cleared before applying a bin increment batch
     * during the first scan, so that finished elements are not affected.
     *
     * This may save some CPU cycles when either
     * `tcspc::histogram_array_progress_event` is being ignored or (less
     * commonly) the bucket source guarantees zero-initialized buckets. In both
     * of these cases, the effect of this flag is unobservable.
     */
    no_clear_new_bucket = 1 << 5,
};

/** \private */
constexpr auto operator|(histogram_policy lhs,
                         histogram_policy rhs) noexcept -> histogram_policy {
    using U = std::underlying_type_t<histogram_policy>;
    return static_cast<histogram_policy>(static_cast<U>(lhs) |
                                         static_cast<U>(rhs));
}

/** \private */
constexpr auto operator&(histogram_policy lhs,
                         histogram_policy rhs) noexcept -> histogram_policy {
    using U = std::underlying_type_t<histogram_policy>;
    return static_cast<histogram_policy>(static_cast<U>(lhs) &
                                         static_cast<U>(rhs));
}

/** \private */
constexpr auto operator~(histogram_policy hp) noexcept -> histogram_policy {
    using U = std::underlying_type_t<histogram_policy>;
    return static_cast<histogram_policy>(~static_cast<U>(hp));
}

/** \private */
constexpr auto operator|=(histogram_policy &lhs, histogram_policy rhs) noexcept
    -> histogram_policy & {
    return lhs = lhs | rhs;
}

/** \private */
constexpr auto operator&=(histogram_policy &lhs, histogram_policy rhs) noexcept
    -> histogram_policy & {
    return lhs = lhs & rhs;
}

} // namespace tcspc
