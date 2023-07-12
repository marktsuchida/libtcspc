/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"

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
 * numbers) very slightly more readable: <tt>event{{macrotime}, other
 * fields}</tt> rather than <tt>event{macrotime, other fields}</tt>.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct base_time_tagged_event {
    static_assert(std::is_integral_v<typename DataTraits::abstime_type>);

    /**
     * \brief The absolute macrotime of this event.
     *
     * The macrotime is the monotonically increasing (strictly speaking,
     * non-decreasing) timestamp assigned to events by time tagging hardware,
     * after processing to eliminate wraparounds.
     *
     * The physical units of the macrotime are dependent on the input data and
     * it is the user's responsibility to interpret correctly. libtcspc is
     * designed to use integer values to preserve exact discretized values and
     * does not handle physical units.
     */
    typename DataTraits::abstime_type macrotime;
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
 * readable: <tt>event{{{macrotime}, channel}, other fields}</tt> rather than
 * <tt>event{macrotime, channel, other fields}</tt>.
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
 * \brief Event indicating latest macrotime reached.
 *
 * \ingroup events-timing
 *
 * Data sources emit this event to indicated that a macrotime stamp has been
 * seen, without any associated event.
 *
 * This conveys useful information because timestamps are monotonic: if a
 * timestamp is observed, it guarantees that all photons (and other events)
 * prior to that time have already been observed.
 *
 * Data sources reading raw device event streams should typically emit this
 * event when a macrotime overflow occurs. Data sources that do not encode
 * such overflows should emit this event once before finishing the stream, if
 * the acquisition duration is known, to indicate the end time point.
 *
 * Note that this event is generally only emitted when the timestamp is not
 * associated with an actual event (photon, marker, etc.).
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct time_reached_event : base_time_tagged_event<DataTraits> {
    /** \brief Equality comparison operator. */
    friend auto operator==(time_reached_event const &lhs,
                           time_reached_event const &rhs) noexcept -> bool {
        return lhs.macrotime == rhs.macrotime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(time_reached_event const &lhs,
                           time_reached_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, time_reached_event const &e)
        -> std::ostream & {
        return s << "time_reached(" << e.macrotime << ')';
    }
};

/**
 * \brief Event indicating loss of data due to buffer overflow.
 *
 * \ingroup events-timing
 *
 * Event producers should continue to produce subsequent photon events, if any;
 * it is the event processor's responsibility to cancel processing, if that is
 * what is desired.
 *
 * Different vendors use different terminology: the overflow may occur in the
 * device FIFO, DMA buffer, or any other stage involved in streaming data to
 * the computer.
 *
 * The macrotime may have skipped some elapsed time when this event occurs;
 * both counts and markers may have been lost.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <typename DataTraits = default_data_traits>
struct data_lost_event : base_time_tagged_event<DataTraits> {
    /** \brief Equality comparison operator. */
    friend auto operator==(data_lost_event const &lhs,
                           data_lost_event const &rhs) noexcept -> bool {
        return lhs.macrotime == rhs.macrotime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(data_lost_event const &lhs,
                           data_lost_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, data_lost_event const &e)
        -> std::ostream & {
        return s << "data_lost(" << e.macrotime << ')';
    }
};

/**
 * \brief Event indicating beginning of interval in which counts were lost.
 *
 * \ingroup events-timing
 *
 * The interval must be ended with a subsequent end_lost_interval_event.
 *
 * Unlike with data_lost_event, the macrotime must remain consistent before,
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
        return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel &&
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
        return s << "untagged_counts(" << e.macrotime << ", " << e.channel
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
        return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(detection_event const &lhs,
                           detection_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, detection_event const &e)
        -> std::ostream & {
        return s << "detection(" << e.macrotime << ", " << e.channel << ')';
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
     * \brief Difference time (a.k.a. microtime, nanotime) of the photon.
     *
     * This is usually the time difference between the photon and
     * synchronization signal, generated by TCSPC electronics. It may or may
     * not be inverted.
     */
    typename DataTraits::difftime_type difftime;

    /** \brief Equality comparison operator. */
    friend auto operator==(time_correlated_detection_event const &lhs,
                           time_correlated_detection_event const &rhs) noexcept
        -> bool {
        return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel &&
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
        return s << "time_correlated_detection(" << e.macrotime << ", "
                 << e.channel << ", " << e.difftime << ')';
    }
};

/**
 * \brief TCSPC event indicating a marker.
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
 * marker_event must be generated for each channel, bearing the same macrotime.
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
        return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(marker_event const &lhs,
                           marker_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, marker_event const &e)
        -> std::ostream & {
        return s << "marker(" << e.macrotime << ", " << e.channel << ')';
    }
};

} // namespace tcspc
