/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"

#include <cstdint>
#include <ostream>

namespace flimevt {

/**
 * \brief Base class for events with a hardware-assigned timestamp.
 */
struct base_time_tagged_event {
    /**
     * \brief The absolute macrotime of this event.
     */
    macrotime macrotime;
};

/**
 * \brief Event indicating latest macrotime reached.
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
 */
struct time_reached_event : public base_time_tagged_event {
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
 */
struct data_lost_event : public base_time_tagged_event {
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
 * The interval must be ended with a subsequent end_lost_interval_event.
 *
 * Unlike with data_lost_event, the macrotime must remain consistent before,
 * during, and after the lost interval.
 *
 * If detected events during the interval could be counted (but not
 * time-tagged), they should be indicated by untagged_counts_event.
 */
struct begin_lost_interval_event : public base_time_tagged_event {
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
 */
struct end_lost_interval_event : public base_time_tagged_event {
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
 * This event should only occur between begin_lost_interval_event and
 * end_lost_interval_event.
 */
struct untagged_counts_event : public base_time_tagged_event {
    /**
     * \brief Number of counts that were detected but could not be time-tagged.
     */
    std::uint32_t count;

    /**
     * \brief The channel on which the counts were detected.
     */
    std::int16_t channel;

    /** \brief Equality comparison operator. */
    friend auto operator==(untagged_counts_event const &lhs,
                           untagged_counts_event const &rhs) noexcept -> bool {
        return lhs.macrotime == rhs.macrotime && lhs.count == rhs.count &&
               lhs.channel == rhs.channel;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(untagged_counts_event const &lhs,
                           untagged_counts_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, untagged_counts_event const &e)
        -> std::ostream & {
        return s << "untagged_counts(" << e.macrotime << ", " << e.count
                 << ", " << e.channel << ')';
    }
};

/**
 * \brief Event indicating a detected count.
 */
struct time_tagged_count_event : public base_time_tagged_event {
    /**
     * \brief The channel on which the count was detected.
     *
     * The channel number may be negative.
     */
    std::int16_t channel;

    /** \brief Equality comparison operator. */
    friend auto operator==(time_tagged_count_event const &lhs,
                           time_tagged_count_event const &rhs) noexcept
        -> bool {
        return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(time_tagged_count_event const &lhs,
                           time_tagged_count_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s, time_tagged_count_event const &e)
        -> std::ostream & {
        return s << "time_tagged_count(" << e.macrotime << ", " << e.channel
                 << ')';
    }
};

/**
 * \brief Event indicating a detected count (typically photon) with difference
 * time.
 */
struct time_correlated_count_event : public base_time_tagged_event {
    /**
     * \brief Difference time (a.k.a. microtime, nanotime) of the photon.
     *
     * This is usually the time difference between the photon and
     * synchronization signal, generated by TCSPC electronics. It may or may
     * not be inverted.
     */
    std::uint16_t difftime;

    /**
     * \brief The channel, or routing signals, of the photon.
     *
     * The channel number may be negative.
     */
    std::int16_t channel;

    /** \brief Equality comparison operator. */
    friend auto operator==(time_correlated_count_event const &lhs,
                           time_correlated_count_event const &rhs) noexcept
        -> bool {
        return lhs.macrotime == rhs.macrotime &&
               lhs.difftime == rhs.difftime && lhs.channel == rhs.channel;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(time_correlated_count_event const &lhs,
                           time_correlated_count_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &s,
                           time_correlated_count_event const &e)
        -> std::ostream & {
        return s << "time_correlated_count(" << e.macrotime << ", "
                 << e.difftime << ", " << e.channel << ')';
    }
};

/**
 * \brief TCSPC event indicating a marker.
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
 */
struct marker_event : public base_time_tagged_event {
    /**
     * \brief Input channel of the marker.
     *
     * Most hardware devices have numbers attched to the marker input channels;
     * the channel number may be negative (e.g., Swabian). The channel
     * numbering may or may not be shared with photon channels, depending on
     * the hardware or data source.
     */
    std::int32_t channel;

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

/**
 * \brief Event set containing all TCSPC events.
 */
using tcspc_events = event_set<time_reached_event, data_lost_event,
                               time_correlated_count_event, marker_event>;

/** \brief Stream insertion operator for TCSPC event variant. */
inline auto
operator<<(std::ostream &os,
           flimevt::event_variant<flimevt::tcspc_events> const &event)
    -> std::ostream & {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

} // namespace flimevt
