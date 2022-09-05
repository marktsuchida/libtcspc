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
 * \brief TCSPC event indicating latest macrotime stamp.
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
struct TimestampEvent : public BaseTimeTaggedEvent {};

inline bool operator==(TimestampEvent const &lhs, TimestampEvent const &rhs) {
    return lhs.macrotime == rhs.macrotime;
}

inline std::ostream &operator<<(std::ostream &s, TimestampEvent const &e) {
    return s << "Timestamp(" << e.macrotime << ')';
}

/**
 * \brief TCSPC event indicating loss of data due to buffer overflow.
 *
 * Event producers should continue to produce subsequent photon events, if any;
 * it is the event processor's responsibility to cancel processing, if that is
 * what is desired.
 *
 * Different vendors use different terminology: the overflow may occur in the
 * device FIFO, DMA buffer, or any other stage involved in streaming data to
 * the computer.
 */
struct DataLostEvent : public BaseTimeTaggedEvent {};

inline bool operator==(DataLostEvent const &lhs, DataLostEvent const &rhs) {
    return lhs.macrotime == rhs.macrotime;
}

inline std::ostream &operator<<(std::ostream &s, DataLostEvent const &e) {
    return s << "DataLost(" << e.macrotime << ')';
}

/**
 * \brief Abstract base class for valid and invalid TCSPC photon events.
 */
struct BasePhotonEvent : public BaseTimeTaggedEvent {
    /**
     * \brief Nanotime (a.k.a. difference time, microtime) of the photon.
     *
     * This is the time difference between the photon and synchronization
     * signal, generated by TCSPC electronics (TAC + ADC or TDC). It may or may
     * not be inverted: the raw format produced by the device should be
     * recorded here.
     */
    std::uint16_t nanotime;

    /**
     * \brief The channel, or routing signals, of the photon.
     *
     * The channel number may be negative.
     */
    std::int16_t channel;
};

/**
 * \brief TCSPC event indicating a detected photon.
 */
struct ValidPhotonEvent : public BasePhotonEvent {};

inline bool operator==(ValidPhotonEvent const &lhs,
                       ValidPhotonEvent const &rhs) {
    return lhs.macrotime == rhs.macrotime && lhs.nanotime == rhs.nanotime &&
           lhs.channel == rhs.channel;
}

inline std::ostream &operator<<(std::ostream &s, ValidPhotonEvent const &e) {
    return s << "ValidPhoton(" << e.macrotime << ", " << e.nanotime << ", "
             << e.channel << ')';
}

/**
 * \brief TCSPC event indicating an invalid photon, produced by some devices.
 *
 * These events should be discarded for processing, but can be retained in
 * order to reproduce the original data stream.
 */
struct InvalidPhotonEvent : public BasePhotonEvent {};

inline bool operator==(InvalidPhotonEvent const &lhs,
                       InvalidPhotonEvent const &rhs) {
    return lhs.macrotime == rhs.macrotime && lhs.nanotime == rhs.nanotime &&
           lhs.channel == rhs.channel;
}

inline std::ostream &operator<<(std::ostream &s, InvalidPhotonEvent const &e) {
    return s << "InvalidPhoton(" << e.macrotime << ", " << e.nanotime << ", "
             << e.channel << ')';
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

inline bool operator==(MarkerEvent const &lhs, MarkerEvent const &rhs) {
    return lhs.macrotime == rhs.macrotime && lhs.channel == rhs.channel;
}

inline std::ostream &operator<<(std::ostream &s, MarkerEvent const &e) {
    return s << "Marker(" << e.macrotime << ", " << e.channel << ')';
}

/**
 * \brief Event set containing all TCSPC events.
 */
using TCSPCEvents = EventSet<TimestampEvent, DataLostEvent, ValidPhotonEvent,
                             InvalidPhotonEvent, MarkerEvent>;

inline std::ostream &
operator<<(std::ostream &os,
           flimevt::EventVariant<flimevt::TCSPCEvents> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

} // namespace flimevt
