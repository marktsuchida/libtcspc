#pragma once

#include "Common.hpp"
#include "EventSet.hpp"
#include "ReadBytes.hpp"
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
    unsigned char bytes[4];

    inline static constexpr Macrotime MacrotimeOverflowPeriod = 1 << 12;

    std::uint16_t GetADCValue() const noexcept {
        std::uint8_t lo8 = bytes[2];
        std::uint8_t hi4 = bytes[3] & 0x0f;
        return lo8 | (std::uint16_t(hi4) << 8);
    }

    std::uint8_t GetRoutingSignals() const noexcept {
        // The documentation somewhat confusingly says that these bits are
        // "inverted", but what they mean is that the TTL inputs are active
        // low. The bits in the FIFO data are not inverted.
        return unsigned(bytes[1]) >> 4;
    }

    std::uint16_t GetMacrotime() const noexcept {
        return unsigned(internal::ReadU16LE(&bytes[0])) & 0x0fffU;
    }

    bool GetMarkerFlag() const noexcept {
        return unsigned(bytes[3]) & (1U << 4);
    }

    std::uint8_t GetMarkerBits() const noexcept { return GetRoutingSignals(); }

    bool GetGapFlag() const noexcept { return unsigned(bytes[3]) & (1U << 5); }

    bool GetMacrotimeOverflowFlag() const noexcept {
        return unsigned(bytes[3]) & (1U << 6);
    }

    bool GetInvalidFlag() const noexcept {
        return unsigned(bytes[3]) & (1U << 7);
    }

    bool IsMultipleMacrotimeOverflow() const noexcept {
        // Although documentation is not clear, a marker can share an event
        // record with a (single) macrotime overflow, just as a photon can.
        return GetMacrotimeOverflowFlag() && GetInvalidFlag() &&
               !GetMarkerFlag();
    }

    // Get the 28-bit macro timer overflow count
    std::uint32_t GetMultipleMacrotimeOverflowCount() const noexcept {
        return internal::ReadU32LE(&bytes[0]) & 0x0fffffffU;
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 4096-channel mode.
 */
struct BHSPC600Event48 {
    unsigned char bytes[6];

    inline static constexpr Macrotime MacrotimeOverflowPeriod = 1 << 24;

    std::uint16_t GetADCValue() const noexcept {
        return unsigned(internal::ReadU16LE(&bytes[0])) & 0x0fffU;
    }

    std::uint8_t GetRoutingSignals() const noexcept { return bytes[3]; }

    std::uint32_t GetMacrotime() const noexcept {
        auto lo8 = unsigned(bytes[4]);
        auto mid8 = unsigned(bytes[5]);
        auto hi8 = unsigned(bytes[2]);
        return lo8 | (mid8 << 8) | (hi8 << 16);
    }

    bool GetMarkerFlag() const noexcept { return false; }

    std::uint8_t GetMarkerBits() const noexcept { return 0; }

    bool GetGapFlag() const noexcept { return unsigned(bytes[1]) & (1U << 6); }

    bool GetMacrotimeOverflowFlag() const noexcept {
        return unsigned(bytes[1]) & (1U << 5);
    }

    bool GetInvalidFlag() const noexcept {
        return unsigned(bytes[1]) & (1U << 4);
    }

    bool IsMultipleMacrotimeOverflow() const noexcept { return false; }

    std::uint32_t GetMultipleMacrotimeOverflowCount() const noexcept {
        return 0;
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 256-channel mode.
 */
struct BHSPC600Event32 {
    unsigned char bytes[4];

    inline static constexpr Macrotime MacrotimeOverflowPeriod = 1 << 17;

    std::uint16_t GetADCValue() const noexcept { return bytes[0]; }

    std::uint8_t GetRoutingSignals() const noexcept {
        return (bytes[3] & 0x0f) >> 1;
    }

    std::uint32_t GetMacrotime() const noexcept {
        auto lo8 = unsigned(bytes[1]);
        auto mid8 = unsigned(bytes[2]);
        auto hi1 = unsigned(bytes[3]) & 1U;
        return lo8 | (mid8 << 8) | (hi1 << 16);
    }

    bool GetMarkerFlag() const noexcept { return false; }

    std::uint8_t GetMarkerBits() const noexcept { return 0; }

    bool GetGapflag() const noexcept { return unsigned(bytes[3]) & (1U << 5); }

    bool GetMacrotimeOverflowFlag() const noexcept {
        return unsigned(bytes[3]) & (1U << 6);
    }

    bool GetInvalidFlag() const noexcept {
        return unsigned(bytes[3]) & (1U << 7);
    }

    bool IsMultipleMacrotimeOverflow() const noexcept { return false; }

    std::uint32_t GetMultipleMacrotimeOverflowCount() const noexcept {
        return 0;
    }
};

namespace internal {

// Common implementation for DecodeBHSPC, DecodeBHSPC600_48, DecodeBHSPC600_32.
// E is the binary record event class.
template <typename E, typename D> class BaseDecodeBHSPC {
    Macrotime macrotimeBase; // Time of last overflow
    Macrotime lastMacrotime;

    D downstream;

  public:
    explicit BaseDecodeBHSPC(D &&downstream)
        : macrotimeBase(0), lastMacrotime(0),
          downstream(std::move(downstream)) {}

    // Rule of zero

    void HandleEvent(E const &event) noexcept {
        if (event.IsMultipleMacrotimeOverflow()) {
            macrotimeBase += E::MacrotimeOverflowPeriod *
                             event.GetMultipleMacrotimeOverflowCount();

            TimeReachedEvent e;
            e.macrotime = macrotimeBase;
            downstream.HandleEvent(e);
            return;
        }

        if (event.GetMacrotimeOverflowFlag()) {
            macrotimeBase += E::MacrotimeOverflowPeriod;
        }

        Macrotime macrotime = macrotimeBase + event.GetMacrotime();

        // Validate input: ensure macrotime increases monotonically (a common
        // assumption made by downstream processors)
        if (macrotime <= lastMacrotime) {
            downstream.HandleEnd(std::make_exception_ptr(
                std::runtime_error("Non-monotonic macrotime encountered")));
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
            std::uint32_t bits = event.GetMarkerBits();
            while (bits) {
                e.channel = internal::CountTrailingZeros32(bits);
                downstream.HandleEvent(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        if (event.GetInvalidFlag()) {
            TimeReachedEvent e;
            e.macrotime = macrotime;
            downstream.HandleEvent(e);
        } else {
            TimeCorrelatedCountEvent e;
            e.macrotime = macrotime;
            e.nanotime = event.GetADCValue();
            e.channel = event.GetRoutingSignals();
            downstream.HandleEvent(e);
        }
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};
} // namespace internal

/**
 * \brief Processor that decodes raw BH SPC (most models) events.
 *
 * \see DecodeBHSPC()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class DecodeBHSPC : public internal::BaseDecodeBHSPC<BHSPCEvent, D> {
  public:
    using internal::BaseDecodeBHSPC<BHSPCEvent, D>::BaseDecodeBHSPC;
};

/**
 * \brief Deduction guide for constructing a DecodeBHSPC processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D> DecodeBHSPC(D &&downstream) -> DecodeBHSPC<D>;

/**
 * \brief Processor that decodes raw BH SPC-600/630 events in 4096-channel
 * mode.
 *
 * \see DecodeBHSPC600_48()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class DecodeBHSPC600_48
    : public internal::BaseDecodeBHSPC<BHSPC600Event48, D> {
  public:
    using internal::BaseDecodeBHSPC<BHSPC600Event48, D>::BaseDecodeBHSPC;
};

/**
 * \brief Deduction guide for constructing a DecodeBHSPC600_48 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
DecodeBHSPC600_48(D &&downstream) -> DecodeBHSPC600_48<D>;

/**
 * \brief Processor that decodes raw BH SPC-600/630 events in 256-channel mode.
 *
 * \see DecodeBHSPC600_32()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class DecodeBHSPC600_32
    : public internal::BaseDecodeBHSPC<BHSPC600Event32, D> {
  public:
    using internal::BaseDecodeBHSPC<BHSPC600Event32, D>::BaseDecodeBHSPC;
};

/**
 * \brief Deduction guide for constructing a DecodeBHSPC600_32 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
DecodeBHSPC600_32(D &&downstream) -> DecodeBHSPC600_32<D>;

/**
 * \brief Event set for raw BH SPC data stream.
 */
using BHSPCEvents = EventSet<BHSPCEvent>;

/**
 * \brief Event set for raw BH SPC-600/630 data stream in 4096-channel mode.
 */
using BHSPC600Events48 = EventSet<BHSPC600Event48>;

/**
 * \brief Event set for raw BH SPC-600/630 data stream in 256-channel mode.
 */
using BHSPC600Events32 = EventSet<BHSPC600Event32>;

} // namespace flimevt
