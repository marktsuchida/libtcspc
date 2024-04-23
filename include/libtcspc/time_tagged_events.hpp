/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <cstdint>
#include <ostream>
#include <type_traits>

namespace tcspc {

/**
 * \brief Event indicating latest abstime reached.
 *
 * \ingroup events-tcspc
 *
 * This event has two purposes:
 * -# To convey the passage of abstime even when there are otherwise no events,
 *    including the information that a certain time point has been reached
 *    (this is sometimes necessary to detect the end of a measurement when
 *    there are no detections)
 * -# To ensure that a (branch of) the processing graph will not be devoid of
 *    events for long stretches of abstime (and, in the case of processing live
 *    acquired data, wall clock time).
 *
 * Data sources (typically device event decoders or the data acquisition
 * itself) should emit this event in order to preserve information about the
 * last time reached by the acquisition (especially at the end of the stream),
 * when otherwise there would be no event emitted. The frequency of
 * time-reached events can be adjusted using `tcspc::regulate_time_reached()`.
 *
 * \see `tcspc::regulate_time_reached()`
 *
 * \tparam DataTraits traits type specifying `abstime_type`
 */
template <typename DataTraits = default_data_traits>
struct time_reached_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief Equality comparison operator. */
    friend auto operator==(time_reached_event const &lhs,
                           time_reached_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(time_reached_event const &lhs,
                           time_reached_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, time_reached_event const &e)
        -> std::ostream & {
        return s << "time_reached(" << e.abstime << ')';
    }
};

/**
 * \brief Event indicating loss of data due to buffer overflow.
 *
 * \ingroup events-tcspc-lost
 *
 * Event producers should generally continue to produce subsequent detection
 * events, if any; it is the event processor's responsibility to cancel
 * processing, if that is what is desired.
 *
 * Different vendors use different terminology: the overflow may occur in the
 * device FIFO, DMA buffer, or any other stage involved in streaming data to
 * the computer.
 *
 * The `abstime` may have skipped some elapsed time when this event occurs;
 * both detections and markers may have been lost.
 *
 * \tparam DataTraits traits type specifying `abstime_type`
 */
template <typename DataTraits = default_data_traits> struct data_lost_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief Equality comparison operator. */
    friend auto operator==(data_lost_event const &lhs,
                           data_lost_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(data_lost_event const &lhs,
                           data_lost_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, data_lost_event const &e)
        -> std::ostream & {
        return s << "data_lost(" << e.abstime << ')';
    }
};

/**
 * \brief Event indicating beginning of interval in which counts were lost.
 *
 * \ingroup events-tcspc-lost
 *
 * The interval must be ended with a subsequent
 * `tcspc::end_lost_interval_event`.
 *
 * Unlike with `tcspc::data_lost_event`, the `abstime` must remain consistent
 * before, during, and after the lost interval.
 *
 * If detected events during the interval could be counted (but not
 * time-tagged), they should be indicated by `tcspc::lost_counts_event`.
 *
 * \tparam DataTraits traits type specifying `abstime_type`
 */
template <typename DataTraits = default_data_traits>
struct begin_lost_interval_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief Equality comparison operator. */
    friend auto operator==(begin_lost_interval_event const &lhs,
                           begin_lost_interval_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(begin_lost_interval_event const &lhs,
                           begin_lost_interval_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, begin_lost_interval_event const &e)
        -> std::ostream & {
        (void)e;
        return s << "begin_lost_interval()";
    }
};

/**
 * \brief Event indicating end of interval in which counts were lost.
 *
 * \ingroup events-tcspc-lost
 *
 * \tparam DataTraits traits type specifying `abstime_type`
 */
template <typename DataTraits = default_data_traits>
struct end_lost_interval_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief Equality comparison operator. */
    friend auto operator==(end_lost_interval_event const &lhs,
                           end_lost_interval_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(end_lost_interval_event const &lhs,
                           end_lost_interval_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, end_lost_interval_event const &e)
        -> std::ostream & {
        (void)e;
        return s << "end_lost_interval()";
    }
};

/**
 * \brief Event indicating number of counts that could not be time-tagged.
 *
 * \ingroup events-tcspc-lost
 *
 * This event should only occur between `tcspc::begin_lost_interval_event` and
 * `tcspc::end_lost_interval_event`.
 *
 * \tparam DataTraits traits type specifying `abstime_type` and `channel_type`
 */
template <typename DataTraits = default_data_traits> struct lost_counts_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief The channel on which this event occurred. */
    typename DataTraits::channel_type channel;
    static_assert(std::is_integral_v<decltype(channel)>);

    /**
     * \brief Number of counts that were detected but could not be time-tagged.
     */
    std::uint32_t count;

    /** \brief Equality comparison operator. */
    friend auto operator==(lost_counts_event const &lhs,
                           lost_counts_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.channel == rhs.channel &&
               lhs.count == rhs.count;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(lost_counts_event const &lhs,
                           lost_counts_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, lost_counts_event const &e)
        -> std::ostream & {
        return s << "lost_counts(" << e.abstime << ", " << e.channel << ", "
                 << e.count << ')';
    }
};

/**
 * \brief Event indicating number of detections from a non-time-tagging device.
 *
 * \ingroup events-tcspc
 *
 * This event represents detection counts from a device that does not time-tag
 * individual detections but emits counter values at some interval (usually
 * based on some external or internal clock signal).
 *
 * \tparam DataTraits traits type specifying `abstime_type` and `channel_type`
 */
template <typename DataTraits = default_data_traits> struct bulk_counts_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief The channel on which this event occurred. */
    typename DataTraits::channel_type channel;
    static_assert(std::is_integral_v<decltype(channel)>);

    /**
     * \brief Number of non-time-tagged counts detected.
     */
    std::uint32_t count;

    /** \brief Equality comparison operator. */
    friend auto operator==(bulk_counts_event const &lhs,
                           bulk_counts_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.channel == rhs.channel &&
               lhs.count == rhs.count;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bulk_counts_event const &lhs,
                           bulk_counts_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, bulk_counts_event const &e)
        -> std::ostream & {
        return s << "bulk_counts(" << e.abstime << ", " << e.channel << ", "
                 << e.count << ')';
    }
};

/**
 * \brief Event indicating a detected count.
 *
 * \ingroup events-tcspc
 *
 * \tparam DataTraits traits type specifying `abstime_type` and `channel_type`
 */
template <typename DataTraits = default_data_traits> struct detection_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief The channel on which this event occurred. */
    typename DataTraits::channel_type channel;
    static_assert(std::is_integral_v<decltype(channel)>);

    /** \brief Equality comparison operator. */
    friend auto operator==(detection_event const &lhs,
                           detection_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.channel == rhs.channel;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(detection_event const &lhs,
                           detection_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, detection_event const &e)
        -> std::ostream & {
        return s << "detection(" << e.abstime << ", " << e.channel << ')';
    }
};

/**
 * \brief Event indicating a detected count (typically photon) with difference
 * time.
 *
 * \ingroup events-tcspc
 *
 * \tparam DataTraits traits type specifying `abstime_type`, `channel_type`,
 * and `difftime_type`
 */
template <typename DataTraits = default_data_traits>
struct time_correlated_detection_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief The channel on which this event occurred. */
    typename DataTraits::channel_type channel;
    static_assert(std::is_integral_v<decltype(channel)>);

    /**
     * \brief Difference time (a.k.a. microtime, nanotime) associated with the
     * detected event.
     */
    typename DataTraits::difftime_type difftime;
    static_assert(std::is_integral_v<decltype(difftime)>);

    /** \brief Equality comparison operator. */
    friend auto operator==(time_correlated_detection_event const &lhs,
                           time_correlated_detection_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime && lhs.channel == rhs.channel &&
               lhs.difftime == rhs.difftime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(time_correlated_detection_event const &lhs,
                           time_correlated_detection_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s,
                           time_correlated_detection_event const &e)
        -> std::ostream & {
        return s << "time_correlated_detection(" << e.abstime << ", "
                 << e.channel << ", " << e.difftime << ')';
    }
};

/**
 * \brief Event indicating a timing marker.
 *
 * \ingroup events-tcspc
 *
 * These events indicate the timing of some process (e.g. laser scanning) in
 * the acquisition and are generated by external triggers or internally.
 *
 * Becker & Hickl calls these (frame, line, or pixel) markers. PicoQuant calls
 * these external markers. Swabian does not have a dedicated marker facility.
 *
 * Some devices produce single events with potentially multiple markers on
 * different channels, using, e.g., a bitmask. In such cases, a separate
 * `tcspc::marker_event` must be generated for each channel, bearing the same
 * `abstime`. Ordering of simultaneous marker events within the stream is
 * undefined (but ordering should be made deterministic when arbitrarily
 * determined by software).
 *
 * The `channel` numbering of marker events may or may not be shared with
 * detection channels, depending on the hardware or data source.
 *
 * \tparam DataTraits traits type specifying `abstime_type` and `channel_type`
 */
template <typename DataTraits = default_data_traits> struct marker_event {
    /** \brief The absolute time (a.k.a. macrotime) of this event. */
    typename DataTraits::abstime_type abstime;
    static_assert(std::is_integral_v<decltype(abstime)>);

    /** \brief The channel on which this event occurred. */
    typename DataTraits::channel_type channel;
    static_assert(std::is_integral_v<decltype(channel)>);

    /** \brief Equality comparison operator. */
    friend auto operator==(marker_event const &lhs,
                           marker_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.channel == rhs.channel;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(marker_event const &lhs,
                           marker_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, marker_event const &e)
        -> std::ostream & {
        return s << "marker(" << e.abstime << ", " << e.channel << ')';
    }
};

/**
 * \brief Event representing a pair of detection events.
 *
 * \ingroup events-tcspc
 *
 * This event contains a pair of `tcspc::detection_event` instances. Note that
 * it does not contain an `abstime` field and therefore cannot be handled
 * directly by processors that expect a time-stamped event stream.
 *
 * \tparam DataTraits traits type specifying `abstime_type` and `channel_type`
 */
template <typename DataTraits = default_data_traits>
struct detection_pair_event {
    /**
     * \brief The first detection event of the pair.
     *
     * In general, \c first.abstime is out of order.
     */
    detection_event<DataTraits> first;

    /**
     * \brief The second detection event of the pair.
     *
     * Usually, \c second.abstime is in order in the stream (check the
     * documentation for the pairing processor producing the pair stream.)
     */
    detection_event<DataTraits> second;

    /** \brief Equality comparison operator. */
    friend auto operator==(detection_pair_event const &lhs,
                           detection_pair_event const &rhs) noexcept -> bool {
        return lhs.first == rhs.first && lhs.second == rhs.second;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(detection_pair_event const &lhs,
                           detection_pair_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, detection_pair_event const &e)
        -> std::ostream & {
        return s << "detection_pair(" << e.first << ", " << e.second << ')';
    }
};

} // namespace tcspc
