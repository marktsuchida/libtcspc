#pragma once

#include "EventSet.hpp"
#include "TCSPCEvents.hpp"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

namespace flimevt {

// WARNING: I implemented PicoQuant event types to verify that the framework
// can handle a good range of raw event stream formats. But it has not been
// excercised.

// PicoQuant raw photon event ("TTTR") formats are documented in the html files
// contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

// Note that code here is written to run on little- or big-endian machines; see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html

// The two T3 formats (PQPicoT3Event and PQHydraT3Event) use matching member
// names for static polymorphism. This allows PQT3EventDecoder<E> to handle 3
// different formats with the same code.

/**
 * \brief Binary record interpretation for PicoHarp T3 Format.
 *
 * RecType 0x00010303.
 */
struct PQPicoT3Event {
    char bytes[4];

    static Macrotime const NSyncOverflowPeriod = 65536;

    std::uint8_t GetChannel() const noexcept {
        return std::uint8_t(bytes[3]) >> 4;
    }

    std::uint16_t GetDTime() const noexcept {
        std::uint8_t lo8 = bytes[2];
        std::uint8_t hi4 = bytes[3] & 0x0f;
        return lo8 | (std::uint16_t(hi4) << 8);
    }

    std::uint16_t GetNSync() const noexcept {
        return bytes[0] | (std::uint16_t(bytes[1]) << 8);
    }

    bool IsSpecial() const noexcept { return GetChannel() == 15; }

    bool IsNSyncOverflow() const noexcept {
        return IsSpecial() && GetDTime() == 0;
    }

    std::uint16_t GetNSyncOverflowCount() const noexcept { return 1; }

    bool IsExternalMarker() const noexcept {
        return IsSpecial() && GetDTime() != 0;
    }

    std::uint16_t GetExternalMarkerBits() const noexcept { return GetDTime(); }
};

/**
 * \brief Binary record interpretation for HydraHarp, MultiHarp, and
 * TimeHarp260 T3 format.
 *
 * \tparam IsHydraV1 if true, interpret as HydraHarp V1 (RecType 0x00010304)
 * format, in which nsync overflow records always indicate a single overflow
 */
template <bool IsHydraV1> struct PQHydraT3Event {
    std::uint8_t bytes[4];

    static Macrotime const NSyncOverflowPeriod = 1024;

    bool GetSpecialFlag() const noexcept { return bytes[3] & (1 << 7); }

    std::uint8_t GetChannel() const noexcept { return (bytes[3] & 0x7f) >> 1; }

    std::uint16_t GetDTime() const noexcept {
        std::uint8_t lo6 = std::uint8_t(bytes[1]) >> 2;
        std::uint8_t mid8 = bytes[2];
        std::uint8_t hi1 = bytes[3] & 0x01;
        return lo6 | (std::uint16_t(mid8) << 6) | (std::uint16_t(hi1) << 14);
    }

    std::uint16_t GetNSync() const noexcept {
        std::uint8_t lo8 = bytes[0];
        std::uint8_t hi2 = bytes[1] & 0x03;
        return lo8 | (std::uint16_t(hi2) << 8);
    }

    bool IsSpecial() const noexcept { return GetSpecialFlag(); }

    bool IsNSyncOverflow() const noexcept {
        return IsSpecial() && GetChannel() == 63;
    }

    std::uint16_t GetNSyncOverflowCount() const noexcept {
        if (IsHydraV1 || GetNSync() == 0) {
            return 1;
        }
        return GetNSync();
    }

    bool IsExternalMarker() const noexcept {
        return IsSpecial() && GetChannel() != 63;
    }

    std::uint8_t GetExternalMarkerBits() const noexcept {
        return GetChannel();
    }
};

using PQHydraV1T3Event = PQHydraT3Event<true>;
using PQHydraV2T3Event = PQHydraT3Event<false>;

namespace internal {
/**
 * \brief Decode PicoQuant T3 event stream.
 *
 * User code should normally use one of the following concrete classes:
 * PQPicoT3EventDecoder, PQHydraV1T3EventDecoder, PQHydraV2T3EventDecoder.
 *
 * \tparam E binary record interpreter class
 */
template <typename E, typename D> class PQT3EventDecoder {
    Macrotime nSyncBase;
    Macrotime lastNSync;

    D downstream;

  public:
    explicit PQT3EventDecoder(D &&downstream)
        : nSyncBase(0), lastNSync(0), downstream(std::move(downstream)) {}

    void HandleEvent(E const &event) noexcept {
        if (event.IsNSyncOverflow()) {
            nSyncBase +=
                E::NSyncOverflowPeriod * event.GetNSyncOverflowCount();

            TimestampEvent e;
            e.macrotime = nSyncBase;
            downstream.HandleEvent(e);
            return;
        }

        Macrotime nSync = nSyncBase + event.GetNSync();

        // Validate input: ensure nSync increases monotonically (a common
        // assumption made by downstream processors)
        if (nSync <= lastNSync) {
            downstream.HandleError(std::make_exception_ptr(
                std::runtime_error("Non-monotonic nsync encountered")));
            return;
        }
        lastNSync = nSync;

        if (event.IsExternalMarker()) {
            MarkerEvent e;
            e.macrotime = nSync;
            e.bits = event.GetExternalMarkerBits();
            downstream.HandleEvent(e);
            return;
        }

        ValidPhotonEvent e;
        e.macrotime = nSync;
        e.microtime = event.GetDTime();
        e.route = event.GetChannel();
        downstream.HandleEvent(e);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleError(error);
    }
};
} // namespace internal

template <typename D>
using PQPicoT3EventDecoder = internal::PQT3EventDecoder<PQPicoT3Event, D>;

template <typename D>
using PQHydraV1T3EventDecoder =
    internal::PQT3EventDecoder<PQHydraV1T3Event, D>;

template <typename D>
using PQHydraV2T3EventDecoder =
    internal::PQT3EventDecoder<PQHydraV2T3Event, D>;

using PQPicoT3Events = EventSet<PQPicoT3Event>;
using PQHydraV1T3Events = EventSet<PQHydraV1T3Event>;
using PQHydraV2T3Events = EventSet<PQHydraV2T3Event>;

} // namespace flimevt
