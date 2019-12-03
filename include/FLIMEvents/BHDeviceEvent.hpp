#pragma once

#include "DeviceEvent.hpp"


// Raw photon event data formats are documented in The bh TCSPC Handbook (see
// section on FIFO Files in the chapter on Data file structure).

// Note that code here is written to run on little- or big-endian machines; see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html

/**
 * \brief Binary record interpretation for raw BH SPC event.
 *
 * This interprets the FIFO format used by most BH SPC models, except for
 * SPC-600 and SPC-630.
 */
struct BHSPCEvent {
    uint8_t bytes[4];

    static uint64_t const MacroTimeOverflowPeriod = 1 << 12;

    uint16_t GetADCValue() const noexcept {
        uint8_t lo8 = bytes[2];
        uint8_t hi4 = bytes[3] & 0x0f;
        return lo8 | (uint16_t(hi4) << 8);
    }

    uint8_t GetRoutingSignals() const noexcept {
        // The documentation somewhat confusingly says that these bits are
        // "inverted", but what they mean is that the TTL inputs are active
        // low. The bits in the FIFO data are not inverted.
        return bytes[1] >> 4;
    }

    uint16_t GetMacroTime() const noexcept {
        uint8_t lo8 = bytes[0];
        uint8_t hi4 = bytes[1] & 0x0f;
        return lo8 | (uint16_t(hi4) << 8);
    }

    bool GetMarkerFlag() const noexcept {
        return bytes[3] & (1 << 4);
    }

    uint8_t GetMarkerBits() const noexcept {
        return GetRoutingSignals();
    }

    bool GetGapFlag() const noexcept {
        return bytes[3] & (1 << 5);
    }

    bool GetMacroTimeOverflowFlag() const noexcept {
        return bytes[3] & (1 << 6);
    }

    bool GetInvalidFlag() const noexcept {
        return bytes[3] & (1 << 7);
    }

    bool IsMultipleMacroTimeOverflow() const noexcept {
        // Although documentation is not clear, a marker can share an event
        // record with a (single) macro-time overflow, just as a photon can.
        return GetMacroTimeOverflowFlag() && GetInvalidFlag() &&
            !GetMarkerFlag();
    }

    // Get the 27-bit macro timer overflow count
    uint32_t GetMultipleMacroTimeOverflowCount() const noexcept {
        return bytes[0] |
            (uint32_t(bytes[1]) << 8) |
            (uint32_t(bytes[2]) << 16) |
            (uint32_t(bytes[3] & 0x0f) << 24);
    }
};


/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 4096-channel mode.
 */
struct BHSPC600Event48 {
    uint8_t bytes[6];

    static uint64_t const MacroTimeOverflowPeriod = 1 << 24;

    uint16_t GetADCValue() const noexcept {
        uint8_t lo8 = bytes[0];
        uint8_t hi4 = bytes[1] & 0x0f;
        return lo8 | (uint16_t(hi4) << 8);
    }

    uint8_t GetRoutingSignals() const noexcept {
        return bytes[3];
    }

    uint32_t GetMacroTime() const noexcept {
        return bytes[4] | (uint32_t(bytes[5]) << 8) | (uint32_t(bytes[2]) << 16);
    }

    bool GetMarkerFlag() const noexcept {
        return false;
    }

    uint8_t GetMarkerBits() const noexcept {
        return 0;
    }

    bool GetGapFlag() const noexcept {
        return bytes[1] & (1 << 6);
    }

    bool GetMacroTimeOverflowFlag() const noexcept {
        return bytes[1] & (1 << 5);
    }

    bool GetInvalidFlag() const noexcept {
        return bytes[1] & (1 << 4);
    }

    bool IsMultipleMacroTimeOverflow() const noexcept {
        return false;
    }

    uint32_t GetMultipleMacroTimeOverflowCount() const noexcept {
        return 0;
    }
};


/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 256-channel mode.
 */
struct BHSPC600Event32 {
    uint8_t bytes[4];

    static uint64_t const MacroTimeOverflowPeriod = 1 << 17;

    uint16_t GetADCValue() const noexcept {
        return bytes[0];
    }

    uint8_t GetRoutingSignals() const noexcept {
        return (bytes[3] & 0x0f) >> 1;
    }

    uint32_t GetMacroTime() const noexcept {
        uint8_t lo8 = bytes[1];
        uint8_t mid8 = bytes[2];
        uint8_t hi1 = bytes[3] & 0x01;
        return lo8 | (uint16_t(mid8) << 8) | (uint16_t(hi1) << 16);
    }

    bool GetMarkerFlag() const noexcept {
        return false;
    }

    uint8_t GetMarkerBits() const noexcept {
        return 0;
    }

    bool GetGapflag() const noexcept {
        return bytes[3] & (1 << 5);
    }

    bool GetMacroTimeOverflowFlag() const noexcept {
        return bytes[3] & (1 << 6);
    }

    bool GetInvalidFlag() const noexcept {
        return bytes[3] & (1 << 7);
    }

    bool IsMultipleMacroTimeOverflow() const noexcept {
        return false;
    }

    uint32_t GetMultipleMacroTimeOverflowCount() const noexcept {
        return 0;
    }
};


/**
 * \brief Decode BH SPC event stream.
 *
 * User code should normally use one of the following concrete classes:
 * BHSPCEventDecoder, BHSPC600Event48Decoder, BHSPC600Event32Decoder.
 *
 * \tparam E binary record interpreter class
 */
template <typename E>
class BHEventDecoder : public DeviceEventDecoder {
    uint64_t macrotimeBase; // Time of last overflow
    uint64_t lastMacrotime;

public:
    BHEventDecoder(std::shared_ptr<DecodedEventProcessor> downstream) :
        DeviceEventDecoder(downstream),
        macrotimeBase(0),
        lastMacrotime(0)
    {}

    std::size_t GetEventSize() const noexcept override {
        return sizeof(E);
    }

    void HandleDeviceEvent(char const* event) override {
        E const* devEvt = reinterpret_cast<E const*>(event);

        if (devEvt->IsMultipleMacroTimeOverflow()) {
            macrotimeBase += E::MacroTimeOverflowPeriod *
                devEvt->GetMultipleMacroTimeOverflowCount();

            DecodedEvent e;
            e.macrotime = macrotimeBase;
            SendTimestamp(e);
            return;
        }

        if (devEvt->GetMacroTimeOverflowFlag()) {
            macrotimeBase += E::MacroTimeOverflowPeriod;
        }

        uint64_t macrotime = macrotimeBase + devEvt->GetMacroTime();

        // Validate input: ensure macrotime increases monotonically (a common
        // assumption made by downstream processors)
        if (macrotime <= lastMacrotime) {
            SendError("Non-monotonic macro-time encountered");
            return;
        }
        lastMacrotime = macrotime;

        if (devEvt->GetGapFlag()) {
            DataLostEvent e;
            e.macrotime = macrotime;
            SendDataLost(e);
        }

        if (devEvt->GetMarkerFlag()) {
            MarkerEvent e;
            e.macrotime = macrotime;
            e.bits = devEvt->GetMarkerBits();
            SendMarker(e);
            return;
        }

        if (devEvt->GetInvalidFlag()) {
            InvalidPhotonEvent e;
            e.macrotime = macrotime;
            e.microtime = devEvt->GetADCValue();
            e.route = devEvt->GetRoutingSignals();
            SendInvalidPhoton(e);
        }
        else {
            ValidPhotonEvent e;
            e.macrotime = macrotime;
            e.microtime = devEvt->GetADCValue();
            e.route = devEvt->GetRoutingSignals();
            SendValidPhoton(e);
        }
    }
};


using BHSPCEventDecoder = BHEventDecoder<BHSPCEvent>;
using BHSPC600Event48Decoder = BHEventDecoder<BHSPC600Event48>;
using BHSPC600Event32Decoder = BHEventDecoder<BHSPC600Event32>;
