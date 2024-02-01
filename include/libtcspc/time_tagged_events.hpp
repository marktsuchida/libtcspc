/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <cstdint>
#include <ostream>
#include <type_traits>

namespace tcspc {

/**
 * \brief Base class for events with a hardware-assigned timestamp.
 *
 * \ingroup events-timing
 *
 * Event types that are associated with absolute time are derived from this
 * type. However, this is not a requirement for polymorphism; it serves
 * primarily to make aggregate construction (which tends to be a list of
 * numbers) very slightly more readable: <tt>event{{abstime}, other
 * fields}</tt> rather than <tt>event{abstime, other fields}</tt>.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct base_time_tagged_event {
    static_assert(std::is_integral_v<typename DataTraits::abstime_type>);

    /**
     * \brief The absolute time (a.k.a. macrotime) of this event.
     *
     * The abstime is the monotonically increasing (strictly speaking,
     * non-decreasing) timestamp assigned to events by time tagging hardware,
     * after processing to eliminate rollovers.
     *
     * The physical units of the abstime are dependent on the input data and it
     * is the user's responsibility to interpret the values correctly. libtcspc
     * is designed to use integer values to preserve exact discretized values
     * and does not handle physical units.
     */
    typename DataTraits::abstime_type abstime;
};

/**
 * \brief Base class for time-tagged events that have a channel number.
 *
 * \ingroup events-timing
 *
 * Event types that are associated with absolute time and have a run-time
 * channel number are derived from this type. However, this is not a
 * requirement for polymorphism; it serves primarily to make aggregate
 * construction (which tends to be a list of numbers) very slightly more
 * readable: <tt>event{{{abstime}, channel}, other fields}</tt> rather than
 * <tt>event{abstime, channel, other fields}</tt>.
 *
 * Different devices have different ranges for the channel; some use negative
 * numbers.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 */
template <typename DataTraits = default_data_traits>
struct base_channeled_time_tagged_event : base_time_tagged_event<DataTraits> {
    static_assert(std::is_integral_v<typename DataTraits::channel_type>);

    /**
     * \brief The channel on which this event occurred.
     */
    typename DataTraits::channel_type channel;
};

/**
 * \brief Event indicating latest abstime reached.
 *
 * \ingroup events-timing
 *
 * Data sources emit this event to indicated that a abstime stamp has been
 * seen, without any associated event.
 *
 * This conveys useful information because timestamps are monotonic: if a
 * timestamp is observed, it guarantees that all events prior to that time have
 * already been observed. This may be important in determining that data
 * acquisition or processing can be concluded.
 *
 * Data sources reading raw device event streams should typically emit this
 * event when a device event was received that would not result in any other
 * event being emitted. They should also emit a single event of this type at
 * the end of the stream if the ending abstime of the stream is known.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct time_reached_event : base_time_tagged_event<DataTraits> {
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
 * \ingroup events-timing
 *
 * Event producers should generally continue to produce subsequent detection
 * events, if any; it is the event processor's responsibility to cancel
 * processing, if that is what is desired.
 *
 * Different vendors use different terminology: the overflow may occur in the
 * device FIFO, DMA buffer, or any other stage involved in streaming data to
 * the computer.
 *
 * The abstime may have skipped some elapsed time when this event occurs; both
 * counts and markers may have been lost.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct data_lost_event : base_time_tagged_event<DataTraits> {
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
 * \ingroup events-timing
 *
 * The interval must be ended with a subsequent end_lost_interval_event.
 *
 * Unlike with data_lost_event, the abstime must remain consistent before,
 * during, and after the lost interval.
 *
 * If detected events during the interval could be counted (but not
 * time-tagged), they should be indicated by untagged_counts_event.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct begin_lost_interval_event : base_time_tagged_event<DataTraits> {
    /** \brief Equality comparison operator. */
    friend auto operator==(begin_lost_interval_event const &lhs,
                           begin_lost_interval_event const &rhs) noexcept
        -> bool {
        (void)lhs;
        (void)rhs;
        return true;
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
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct end_lost_interval_event : base_time_tagged_event<DataTraits> {
    /** \brief Equality comparison operator. */
    friend auto operator==(end_lost_interval_event const &lhs,
                           end_lost_interval_event const &rhs) noexcept
        -> bool {
        (void)lhs;
        (void)rhs;
        return true;
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
 * \ingroup events-timing
 *
 * This event should only occur between begin_lost_interval_event and
 * end_lost_interval_event.
 *
 * Not to be confused with \ref nontagged_counts_event.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 */
template <typename DataTraits = default_data_traits>
struct untagged_counts_event : base_channeled_time_tagged_event<DataTraits> {
    /**
     * \brief Number of counts that were detected but could not be time-tagged.
     */
    std::uint32_t count;

    /** \brief Equality comparison operator. */
    friend auto operator==(untagged_counts_event const &lhs,
                           untagged_counts_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime && lhs.channel == rhs.channel &&
               lhs.count == rhs.count;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(untagged_counts_event const &lhs,
                           untagged_counts_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, untagged_counts_event const &e)
        -> std::ostream & {
        return s << "untagged_counts(" << e.abstime << ", " << e.channel
                 << ", " << e.count << ')';
    }
};

/**
 * \brief Event indicating number of detections from a non-time-tagging device.
 *
 * \ingroup events-timing
 *
 * Not to be confused with \ref untagged_counts_event.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 */
template <typename DataTraits = default_data_traits>
struct nontagged_counts_event : base_channeled_time_tagged_event<DataTraits> {
    /**
     * \brief Number of non-time-tagged counts detected.
     */
    std::uint32_t count;

    /** \brief Equality comparison operator. */
    friend auto operator==(nontagged_counts_event const &lhs,
                           nontagged_counts_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime && lhs.channel == rhs.channel &&
               lhs.count == rhs.count;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(nontagged_counts_event const &lhs,
                           nontagged_counts_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, nontagged_counts_event const &e)
        -> std::ostream & {
        return s << "nontagged_counts(" << e.abstime << ", " << e.channel
                 << ", " << e.count << ')';
    }
};

/**
 * \brief Event indicating a detected count.
 *
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 */
template <typename DataTraits = default_data_traits>
struct detection_event : base_channeled_time_tagged_event<DataTraits> {
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
 * \ingroup events-timing
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type
 */
template <typename DataTraits = default_data_traits>
struct time_correlated_detection_event
    : base_channeled_time_tagged_event<DataTraits> {
    static_assert(std::is_integral_v<typename DataTraits::difftime_type>);

    /**
     * \brief Difference time (a.k.a. microtime, nanotime) associated with the
     * detected event.
     *
     * This is typically the time difference between the photon and
     * synchronization signal, generated by TCSPC electronics. It may or may
     * not be inverted.
     */
    typename DataTraits::difftime_type difftime;

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
 * \ingroup events-timing
 *
 * These events indicate the timing of some process (e.g. laser scanning) in
 * the acquisition and are generated by external triggers or internally.
 *
 * Becker & Hickl calls these (frame, line, or pixel) markers. PicoQuant calls
 * these external markers.
 *
 * Some devices produce single events with potentially multiple markers on
 * different channels, using, e.g., a bitmask. In such cases, a separate
 * marker_event must be generated for each channel, bearing the same abstime.
 * Ordering of simultaneous marker events within the stream is undefined (but
 * ordering should be made deterministic when arbitrarily determined by
 * software).
 *
 * The channel numbering of marker events may or may not be shared with
 * detection channels, depending on the hardware or data source.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
 */
template <typename DataTraits = default_data_traits>
struct marker_event : base_channeled_time_tagged_event<DataTraits> {
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
 * \ingroup events-timing
 *
 * This event contains a pair of \ref detection_event instances. Note that it
 * does not contain an \c abstime field and therefore cannot be handled
 * directly by processors that expect a time-stamped event stream.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type
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
