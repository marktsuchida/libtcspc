#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

#include <cstdint>

namespace flimevt {

/**
 * \brief Base class for logical TCSPC events (photons, markers, and
 * exceptional conditions).
 *
 * These are "logical" events in the sense that vendor-specific encoding and
 * clock overflow counters have been decoded and processed.
 */
struct TCSPCEvent {
    /**
     * \brief The absolute macrotime of this event.
     *
     * The macrotime is in device- and configuration-specific units;
     * conversion to physical (or other) units (which may result in loss of the
     * exact raw data) is not the concern of this library.
     */
    Macrotime macrotime;
};

/**
 * \brief Event to update macrotime stamp.
 *
 * Data sources emit this event to indicated that a macrotime stamp has been
 * seen, without any associated event.
 *
 * This conveys useful information because timestamps are monotonic: if a
 * timestamp is observed, it guarantees that all photons prior to that time
 * have already been observed.
 *
 * Data sources reading raw device event streams should typically emit this
 * event when a macrotime overflow occurs. Data sources that do not encode
 * such overflows should emit this event once before finishing the stream, if
 * the acquisition duration is known, to indicate the end time point.
 *
 * Note that this event is generally only emitted when the timestamp is not
 * associated with an actual event (photon, marker, etc.).
 */
struct TimestampEvent : public TCSPCEvent {};

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
 */
struct DataLostEvent : public TCSPCEvent {};

/**
 * \brief Abstract base class for valid and invalid photon events.
 */
struct BasePhotonEvent : public TCSPCEvent {
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
     * \brief The route, or channel, of the photon.
     *
     * The lower N bits contain the routing signal for the photon, where N is
     * the number of routing bits supported by the device (_not_ necessarily
     * the number of routing bits enabled for the acquisition).
     */
    std::uint16_t route;
};

/**
 * \brief Event indicating a detected photon.
 */
struct ValidPhotonEvent : public BasePhotonEvent {};

/**
 * \brief Event indicating an invalid photon, produced by some devices.
 */
struct InvalidPhotonEvent : public BasePhotonEvent {};

/**
 * \brief Event indicating a marker.
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
struct MarkerEvent : public TCSPCEvent {
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

using TCSPCEvents = EventSet<TimestampEvent, DataLostEvent, ValidPhotonEvent,
                             InvalidPhotonEvent, MarkerEvent>;

} // namespace flimevt
