/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

#include <cstdint>
#include <ostream>

namespace flimevt {

/**
 * \brief Base class for events with a hardware-assigned timestamp.
 */
struct BaseTimeTaggedEvent {
    /**
     * \brief The absolute macrotime of this event.
     */
    Macrotime macrotime;
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
struct TimeReachedEvent : public BaseTimeTaggedEvent {};

/** \brief Equality operator for TimeReachedEvent. */
constexpr bool operator==(TimeReachedEvent const &lhs,
                          TimeReachedEvent const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime;
}

/** \brief Stream insertion operator for TimeReachedEvent. */
inline std::ostream &operator<<(std::ostream &s, TimeReachedEvent const &e) {
    return s << "TimeReached(" << e.macrotime << ')';
}

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
struct DataLostEvent : public BaseTimeTaggedEvent {};

/** \brief Equality operator for DataLostEvent. */
constexpr bool operator==(DataLostEvent const &lhs,
                          DataLostEvent const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime;
}

/** \brief Stream insertion operator for DataLostEvent. */
inline std::ostream &operator<<(std::ostream &s, DataLostEvent const &e) {
    return s << "DataLost(" << e.macrotime << ')';
}

/**
 * \brief Event indicating beginning of interval in which counts were lost.
 *
 * The interval must be ended with a subsequent EndLostIntervalEvent.
 *
 * Unlike with DataLostEvent, the macrotime must remain consistent before,
 * during, and after the lost interval.
 *
 * If detected events during the interval could be counted (but not
 * time-tagged), they should be indicated by UntaggedCountsEvent.
 */
struct BeginLostIntervalEvent : public BaseTimeTaggedEvent {};

/** \brief Equality operator for BeginLostIntervalEvent. */
constexpr bool operator==(BeginLostIntervalEvent const &lhs,
                          BeginLostIntervalEvent const &rhs) noexcept {
    (void)lhs;
    (void)rhs;
    return true;
}

/** \brief Stream insertion operator for BeginLostIntervalEvent. */
inline std::ostream &operator<<(std::ostream &s,
                                BeginLostIntervalEvent const &e) {
    (void)e;
    return s << "BeginLostIntervalEvent()";
}

/**
 * \brief Event indicating end of interval in which counts were lost.
 */
struct EndLostIntervalEvent : public BaseTimeTaggedEvent {};

/** \brief Equality operator for EndLostIntervalEvent. */
constexpr bool operator==(EndLostIntervalEvent const &lhs,
                          EndLostIntervalEvent const &rhs) noexcept {
    (void)lhs;
    (void)rhs;
    return true;
}

/** \brief Stream insertion operator for EndLostIntervalEvent. */
inline std::ostream &operator<<(std::ostream &s,
                                EndLostIntervalEvent const &e) {
    (void)e;
    return s << "EndLostIntervalEvent()";
}

/**
 * \brief Event indicating number of counts that could not be time-tagged.
 *
 * This event should only occur between BeginLostIntervalEvent and
 * EndLostIntervalEvent.
 */
struct UntaggedCountsEvent : public BaseTimeTaggedEvent {
    /**
     * \brief Number of counts that were detected but could not be time-tagged.
     */
    std::uint32_t count;

    /**
     * \brief The channel on which the counts were detected.
     */
    std::int16_t channel;
};

/** \brief Equality operator for UntaggedCountsEvent. */
constexpr bool operator==(UntaggedCountsEvent const &lhs,
                          UntaggedCountsEvent const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.count == rhs.count &&
           lhs.channel == rhs.channel;
}

/** \brief Stream insertion operator for UntaggedCountsEvent. */
inline std::ostream &operator<<(std::ostream &s,
                                UntaggedCountsEvent const &e) {
    return s << "UntaggedCounts(" << e.macrotime << ", " << e.count << ", "
             << e.channel << ')';
}

/**
 * \brief Event indicating a detected count.
 */
struct TimeTaggedCountEvent : public BaseTimeTaggedEvent {
    /**
     * \brief The channel on which the count was detected.
     *
     * The channel number may be negative.
     */
    std::int16_t channel;
};

/** \brief Equality operator for TimeTaggedCountEvent. */
constexpr bool operator==(TimeTaggedCountEvent const &lhs,
                          TimeTaggedCountEvent const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel;
}

/** \brief Stream insertion operator for TimeTaggedCountEvent. */
inline std::ostream &operator<<(std::ostream &s,
                                TimeTaggedCountEvent const &e) {
    return s << "TimeTaggedCount(" << e.macrotime << ", " << e.channel << ')';
}

/**
 * \brief Event indicating a detected count (typically photon) with difference
 * time.
 */
struct TimeCorrelatedCountEvent : public BaseTimeTaggedEvent {
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
};

/** \brief Equality operator for TimeCorrelatedCountEvent. */
constexpr bool operator==(TimeCorrelatedCountEvent const &lhs,
                          TimeCorrelatedCountEvent const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.difftime == rhs.difftime &&
           lhs.channel == rhs.channel;
}

/** \brief Stream insertion operator for TimeCorrelatedCountEvent. */
inline std::ostream &operator<<(std::ostream &s,
                                TimeCorrelatedCountEvent const &e) {
    return s << "TimeCorrelatedCount(" << e.macrotime << ", " << e.difftime
             << ", " << e.channel << ')';
}

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
 * MarkerEvent must be generated for each channel, bearing the same macrotime.
 * Ordering of simultaneous marker events within the stream is undefined (but
 * ordering should be made deterministic when arbitrarily determined by
 * software).
 */
struct MarkerEvent : public BaseTimeTaggedEvent {
    /**
     * \brief Input channel of the marker.
     *
     * Most hardware devices have numbers attched to the marker input channels;
     * the channel number may be negative (e.g., Swabian). The channel
     * numbering may or may not be shared with photon channels, depending on
     * the hardware or data source.
     */
    std::int32_t channel;
};

/** \brief Equality operator for MarkerEvent. */
constexpr bool operator==(MarkerEvent const &lhs,
                          MarkerEvent const &rhs) noexcept {
    return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel;
}

/** \brief Stream insertion operator for MarkerEvent. */
inline std::ostream &operator<<(std::ostream &s, MarkerEvent const &e) {
    return s << "Marker(" << e.macrotime << ", " << e.channel << ')';
}

/**
 * \brief Event set containing all TCSPC events.
 */
using TCSPCEvents = EventSet<TimeReachedEvent, DataLostEvent,
                             TimeCorrelatedCountEvent, MarkerEvent>;

/** \brief Stream insertion operator for TCSPC event variant. */
inline std::ostream &
operator<<(std::ostream &os,
           flimevt::EventVariant<flimevt::TCSPCEvents> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

} // namespace flimevt
