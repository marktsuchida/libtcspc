#pragma once

#include "Common.hpp"
#include "EventSet.hpp"
#include "TCSPCEvents.hpp"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

namespace flimevt {

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
    std::uint8_t bytes[4];

    inline static Macrotime const MacroTimeOverflowPeriod = 1 << 12;

    std::uint16_t GetADCValue() const noexcept {
        std::uint8_t lo8 = bytes[2];
        std::uint8_t hi4 = bytes[3] & 0x0f;
        return lo8 | (std::uint16_t(hi4) << 8);
    }

    std::uint8_t GetRoutingSignals() const noexcept {
        // The documentation somewhat confusingly says that these bits are
        // "inverted", but what they mean is that the TTL inputs are active
        // low. The bits in the FIFO data are not inverted.
        return bytes[1] >> 4;
    }

    std::uint16_t GetMacroTime() const noexcept {
        std::uint8_t lo8 = bytes[0];
        std::uint8_t hi4 = bytes[1] & 0x0f;
        return lo8 | (std::uint16_t(hi4) << 8);
    }

    bool GetMarkerFlag() const noexcept { return bytes[3] & (1 << 4); }

    std::uint8_t GetMarkerBits() const noexcept { return GetRoutingSignals(); }

    bool GetGapFlag() const noexcept { return bytes[3] & (1 << 5); }

    bool GetMacroTimeOverflowFlag() const noexcept {
        return bytes[3] & (1 << 6);
    }

    bool GetInvalidFlag() const noexcept { return bytes[3] & (1 << 7); }

    bool IsMultipleMacroTimeOverflow() const noexcept {
        // Although documentation is not clear, a marker can share an event
        // record with a (single) macro-time overflow, just as a photon can.
        return GetMacroTimeOverflowFlag() && GetInvalidFlag() &&
               !GetMarkerFlag();
    }

    // Get the 27-bit macro timer overflow count
    std::uint32_t GetMultipleMacroTimeOverflowCount() const noexcept {
        return bytes[0] | (std::uint32_t(bytes[1]) << 8) |
               (std::uint32_t(bytes[2]) << 16) |
               (std::uint32_t(bytes[3] & 0x0f) << 24);
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 4096-channel mode.
 */
struct BHSPC600Event48 {
    std::uint8_t bytes[6];

    inline static Macrotime const MacroTimeOverflowPeriod = 1 << 24;

    std::uint16_t GetADCValue() const noexcept {
        std::uint8_t lo8 = bytes[0];
        std::uint8_t hi4 = bytes[1] & 0x0f;
        return lo8 | (std::uint16_t(hi4) << 8);
    }

    std::uint8_t GetRoutingSignals() const noexcept { return bytes[3]; }

    std::uint32_t GetMacroTime() const noexcept {
        return bytes[4] | (std::uint32_t(bytes[5]) << 8) |
               (std::uint32_t(bytes[2]) << 16);
    }

    bool GetMarkerFlag() const noexcept { return false; }

    std::uint8_t GetMarkerBits() const noexcept { return 0; }

    bool GetGapFlag() const noexcept { return bytes[1] & (1 << 6); }

    bool GetMacroTimeOverflowFlag() const noexcept {
        return bytes[1] & (1 << 5);
    }

    bool GetInvalidFlag() const noexcept { return bytes[1] & (1 << 4); }

    bool IsMultipleMacroTimeOverflow() const noexcept { return false; }

    std::uint32_t GetMultipleMacroTimeOverflowCount() const noexcept {
        return 0;
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 256-channel mode.
 */
struct BHSPC600Event32 {
    std::uint8_t bytes[4];

    inline static Macrotime const MacroTimeOverflowPeriod = 1 << 17;

    std::uint16_t GetADCValue() const noexcept { return bytes[0]; }

    std::uint8_t GetRoutingSignals() const noexcept {
        return (bytes[3] & 0x0f) >> 1;
    }

    std::uint32_t GetMacroTime() const noexcept {
        std::uint8_t lo8 = bytes[1];
        std::uint8_t mid8 = bytes[2];
        std::uint8_t hi1 = bytes[3] & 0x01;
        return lo8 | (std::uint16_t(mid8) << 8) | (std::uint16_t(hi1) << 16);
    }

    bool GetMarkerFlag() const noexcept { return false; }

    std::uint8_t GetMarkerBits() const noexcept { return 0; }

    bool GetGapflag() const noexcept { return bytes[3] & (1 << 5); }

    bool GetMacroTimeOverflowFlag() const noexcept {
        return bytes[3] & (1 << 6);
    }

    bool GetInvalidFlag() const noexcept { return bytes[3] & (1 << 7); }

    bool IsMultipleMacroTimeOverflow() const noexcept { return false; }

    std::uint32_t GetMultipleMacroTimeOverflowCount() const noexcept {
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
template <typename E, typename D> class BHEventDecoder {
    Macrotime macrotimeBase; // Time of last overflow
    Macrotime lastMacrotime;

    D downstream;

  public:
    explicit BHEventDecoder(D &&downstream)
        : macrotimeBase(0), lastMacrotime(0),
          downstream(std::move(downstream)) {}

    // Rule of zero

    void HandleEvent(E const &event) noexcept {
        if (event.IsMultipleMacroTimeOverflow()) {
            macrotimeBase += E::MacroTimeOverflowPeriod *
                             event.GetMultipleMacroTimeOverflowCount();

            TimestampEvent e;
            e.macrotime = macrotimeBase;
            downstream.HandleEvent(e);
            return;
        }

        if (event.GetMacroTimeOverflowFlag()) {
            macrotimeBase += E::MacroTimeOverflowPeriod;
        }

        Macrotime macrotime = macrotimeBase + event.GetMacroTime();

        // Validate input: ensure macrotime increases monotonically (a common
        // assumption made by downstream processors)
        if (macrotime <= lastMacrotime) {
            downstream.HandleEnd(std::make_exception_ptr(
                std::runtime_error("Non-monotonic macro-time encountered")));
            return;
        }
        lastMacrotime = macrotime;

        if (event.GetGapFlag()) {
            DataLostEvent e;
            e.macrotime = macrotime;
            downstream.HandleEvent(e);
        }

        if (event.GetMarkerFlag()) {
            MarkerEvent e;
            e.macrotime = macrotime;
            e.bits = event.GetMarkerBits();
            downstream.HandleEvent(e);
            return;
        }

        if (event.GetInvalidFlag()) {
            InvalidPhotonEvent e;
            e.macrotime = macrotime;
            e.microtime = event.GetADCValue();
            e.route = event.GetRoutingSignals();
            downstream.HandleEvent(e);
        } else {
            ValidPhotonEvent e;
            e.macrotime = macrotime;
            e.microtime = event.GetADCValue();
            e.route = event.GetRoutingSignals();
            downstream.HandleEvent(e);
        }
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

template <typename D>
class BHSPCEventDecoder : public BHEventDecoder<BHSPCEvent, D> {
  public:
    using BHEventDecoder<BHSPCEvent, D>::BHEventDecoder;
};

template <typename D> BHSPCEventDecoder(D &&) -> BHSPCEventDecoder<D>;

template <typename D>
class BHSPC600Event48Decoder : public BHEventDecoder<BHSPC600Event48, D> {
  public:
    using BHEventDecoder<BHSPC600Event48, D>::BHEventDecoder;
};

template <typename D>
BHSPC600Event48Decoder(D &&) -> BHSPC600Event48Decoder<D>;

template <typename D>
class BHSPC600Event32Decoder : public BHEventDecoder<BHSPC600Event32, D> {
  public:
    using BHEventDecoder<BHSPC600Event32, D>::BHEventDecoder;
};

template <typename D>
BHSPC600Event32Decoder(D &&) -> BHSPC600Event32Decoder<D>;

using BHSPCEvents = EventSet<BHSPCEvent>;
using BHSPC600Events48 = EventSet<BHSPC600Event48>;
using BHSPC600Events32 = EventSet<BHSPC600Event32>;

} // namespace flimevt
