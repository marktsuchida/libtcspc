#pragma once

#include "DeviceEvent.hpp"

// I implemented PicoQuant event types to verify that the framework can handle
// a good range of raw event stream formats. But it has not been excercised.
#error "This code is untested; use only after testing"

// PicoQuant raw photon event ("TTTR") formats are documented in the html files
// contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

// Note that code here is written to run on little- or big-endian machines; see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html

// The two T3 formats (PicoT3Event and HydraT3Event) use matching member
// names for static polymorphism. This allows PQT3EventDecoder<E> to handle 3
// different formats with the same code.


/**
 * \brief Binary record interpretation for PicoHarp T3 Format.
 *
 * RecType 0x00010303.
 */
struct PicoT3Event {
    char bytes[4];

    static uint64_t const NSyncOverflowPeriod = 65536;

    uint8_t GetChannel() const noexcept {
        return uint8_t(bytes[3]) >> 4;
    }

    uint16_t GetDTime() const noexcept {
        uint8_t lo8 = bytes[2];
        uint8_t hi4 = bytes[3] & 0x0f;
        return lo8 | (uint16_t(hi4) << 8);
    }

    uint16_t GetNSync() const noexcept {
        return bytes[0] | (uint16_t(bytes[1]) << 8);
    }

    bool IsSpecial() const noexcept {
        return GetChannel() == 15;
    }

    bool IsNSyncOverflow() const noexcept {
        return IsSpecial() && GetDTime() == 0;
    }

    uint16_t GetNSyncOverflowCount() const noexcept {
        return 1;
    }

    bool IsExternalMarker() const noexcept {
        return IsSpecial() && GetDTime() != 0;
    }

    uint16_t GetExternalMarkerBits() const noexcept {
        return GetDTime();
    }
};


/**
 * \brief Binary record interpretation for HydraHarp, MultiHarp, and
 * TimeHarp260 T3 format.
 *
 * \tparam IsHydraV1 if true, interpret as HydraHarp V1 (RecType 0x00010304)
 * format, in which nsync overflow records always indicate a single overflow
 */
template <bool IsHydraV1>
struct HydraT3Event {
    uint8_t bytes[4];

    static uint64_t const NSyncOverflowPeriod = 1024;

    bool GetSpecialFlag() const noexcept {
        return bytes[3] & (1 << 7);
    }

    uint8_t GetChannel() const noexcept {
        return (bytes[3] & 0x7f) >> 1;
    }

    uint16_t GetDTime() const noexcept {
        uint8_t lo6 = uint8_t(bytes[1]) >> 2;
        uint8_t mid8 = bytes[2];
        uint8_t hi1 = bytes[3] & 0x01;
        return lo6 | (uint16_t(mid8) << 6) | (uint16_t(hi1) << 14);
    }

    uint16_t GetNSync() const noexcept {
        uint8_t lo8 = bytes[0];
        uint8_t hi2 = bytes[1] & 0x03;
        return lo8 | (uint16_t(hi2) << 8);
    }

    bool IsSpecial() const noexcept {
        return GetSpecialFlag();
    }

    bool IsNSyncOverflow() const noexcept {
        return IsSpecial() && GetChannel() == 63;
    }

    uint16_t GetNSyncOverflowCount() const noexcept {
        if (IsHydraHarpV1 || GetNSync() == 0) {
            return 1;
        }
        return GetNSync();
    }

    bool IsExternalMarker() const noexcept {
        return IsSpecial() && GetChannel() != 63;
    }

    uint8_t GetExternalMarkerBits() const noexcept {
        return GetChannel();
    }
};


/**
 * \brief Decode PicoQuant T3 event stream. 
 *
 * User code should normally use one of the following concrete classes:
 * PQPicoT3EventDecoder, PQHydraV1T3EventDecoder, PQHydraV2T3EventDecoder.
 *
 * \tparam E binary record interpreter class
 */
template <typename E>
class PQT3EventDecoder : public DeviceEventDecoder {
    uint64_t nSyncBase;
    uint64_t lastNSync;

public:
    PQT3EventDecoder() :
        nSyncBase(0),
        lastNSync(0)
    {}

    std::size_t GetEventSize() const override {
        return sizeof(E);
    }

    void HandleDeviceEvent(char const* event) override {
        E const* devEvt = reinterpret_cast<E const*>(event);

        if (devEvt->IsNSyncOverflow()) {
            nSyncBase += E::NSyncOverflowPeriod * devEvt->GetNSyncOverflowCount();

            DecodedEvent e;
            e.macrotime = nSyncBase;
            SendTimestamp(e);
            return;
        }

        uint64_t nSync = nSyncBase + devEvt->GetNSync();

        // Validate input: ensure nSync increases monotonically (a common
        // assumption made by downstream processors)
        if (nSync <= lastNSync) {
            SendError("Non-monotonic nsync encountered");
            return;
        }
        lastNSync = nSync;

        if (devEvt->IsExternalMarker()) {
            MarkerEvent e;
            e.macrotime = nSync;
            e.bits = devEvt->GetExternalMarkerBits();
            SendMarker(e);
            return;
        }

        PhotonEvent e;
        e.macrotime = nSync;
        e.microtime = devEvt->GetDTime();
        e.route = devEvt->GetChannel();
        e.valid = true;
        SendPhoton(e);
    }

    void HandleFinish() override {
        SendFinish();
    }
};


using PQPicoT3EventDecoder = PQT3EventDecoder<PicoT3Event>;
using PQHydraV1T3EventDecoder = PQT3EventDecoder<HydraT3Event<true>>;
using PQHydraV2T3EventDecoder = PQT3EventDecoder<HydraT3Event<false>>;
