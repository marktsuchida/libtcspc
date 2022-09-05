#pragma once

#include "EventSet.hpp"
#include "ReadBytes.hpp"
#include "TimeTaggedEvents.hpp"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

namespace flimevt {

// PicoQuant raw photon event ("TTTR") formats are documented in the html files
// contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

// Note that code here is written to run on little- or big-endian machines; see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html

// The two T3 formats (PQPicoT3Event and PQHydraT3Event) use matching member
// names for static polymorphism. This allows BaseDecodePQT3<E> to handle 3
// different formats with the same code.

/**
 * \brief Binary record interpretation for PicoHarp T3 Format.
 *
 * RecType 0x00010303.
 */
struct PQPicoT3Event {
    unsigned char bytes[4];

    inline static constexpr Macrotime NSyncOverflowPeriod = 65536;

    std::uint8_t GetChannel() const noexcept {
        return unsigned(bytes[3]) >> 4;
    }

    std::uint16_t GetDTime() const noexcept {
        return unsigned(internal::ReadU16LE(&bytes[2])) & 0x0fffU;
    }

    std::uint16_t GetNSync() const noexcept {
        return internal::ReadU16LE(&bytes[0]);
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
 * \brief Abstract base class for binary record interpretation for HydraHarp,
 * MultiHarp, and TimeHarp260 T3 format.
 *
 * \tparam IsHydraV1 if true, interpret as HydraHarp V1 (RecType 0x00010304)
 * format, in which nsync overflow records always indicate a single overflow
 */
template <bool IsHydraV1> struct PQHydraT3Event {
    unsigned char bytes[4];

    inline static constexpr Macrotime NSyncOverflowPeriod = 1024;

    bool GetSpecialFlag() const noexcept {
        return unsigned(bytes[3]) & (1U << 7);
    }

    std::uint8_t GetChannel() const noexcept {
        return (unsigned(bytes[3]) & 0x7fU) >> 1;
    }

    std::uint16_t GetDTime() const noexcept {
        auto lo6 = unsigned(bytes[1]) >> 2;
        auto mid8 = unsigned(bytes[2]);
        auto hi1 = unsigned(bytes[3]) & 1U;
        return lo6 | (mid8 << 6) | (hi1 << 14);
    }

    std::uint16_t GetNSync() const noexcept {
        return unsigned(internal::ReadU16LE(&bytes[0])) & 0x03ffU;
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

/**
 * \brief Binary record interpretation for HydraHarp V1 T3 format.
 */
using PQHydraV1T3Event = PQHydraT3Event<true>;

/**
 * \brief Binary record interpretation for HydraHarp V2, MultiHarp, and
 * TimeHarp260 T3 format.
 */
using PQHydraV2T3Event = PQHydraT3Event<false>;

namespace internal {

// Common implementation for DecodePQPicoT3, DecodePQHydraV1T3,
// DecodePQHydraV2T3.
// E is the binary record event class.
template <typename E, typename D> class BaseDecodePQT3 {
    Macrotime nSyncBase;
    Macrotime lastNSync;

    D downstream;

  public:
    explicit BaseDecodePQT3(D &&downstream)
        : nSyncBase(0), lastNSync(0), downstream(std::move(downstream)) {}

    void HandleEvent(E const &event) noexcept {
        if (event.IsNSyncOverflow()) {
            nSyncBase +=
                E::NSyncOverflowPeriod * event.GetNSyncOverflowCount();

            TimeReachedEvent e;
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
            std::uint32_t bits = event.GetExternalMarkerBits();
            while (bits) {
                e.channel = internal::CountTrailingZeros32(bits);
                downstream.HandleEvent(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        TimeCorrelatedCountEvent e;
        e.macrotime = nSync;
        e.difftime = event.GetDTime();
        e.channel = event.GetChannel();
        downstream.HandleEvent(e);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleError(error);
    }
};
} // namespace internal

/**
 * \brief Processor that decodes PicoQuant PicoHarp T3 events.
 *
 * \see DecodePQPicoT3()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class DecodePQPicoT3 : public internal::BaseDecodePQT3<PQPicoT3Event, D> {
  public:
    using internal::BaseDecodePQT3<PQPicoT3Event, D>::BaseDecodePQT3;
};

/**
 * \brief Deduction guide for constructing a DecodePQPicoT3 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D> DecodePQPicoT3(D &&downstream) -> DecodePQPicoT3<D>;

/**
 * \brief Processor that decodes PicoQuant HydraHarp V1 T3 events.
 *
 * \see DecodePQHydraV1T3()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class DecodePQHydraV1T3
    : public internal::BaseDecodePQT3<PQHydraV1T3Event, D> {
  public:
    using internal::BaseDecodePQT3<PQHydraV1T3Event, D>::BaseDecodePQT3;
};

/**
 * \brief Deduction guide for constructing a DecodePQHydraV1T3 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
DecodePQHydraV1T3(D &&downstream) -> DecodePQHydraV1T3<D>;

/**
 * \brief Processor that decodes PicoQuant HydraHarp V2, MultiHarp, and
 * TimeHarp260 T3 events.
 *
 * \see DecodePQHydraV2T3()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class DecodePQHydraV2T3
    : public internal::BaseDecodePQT3<PQHydraV2T3Event, D> {
  public:
    using internal::BaseDecodePQT3<PQHydraV2T3Event, D>::BaseDecodePQT3;
};

/**
 * \brief Deduction guide for constructing a DecodePQHydraV2T3 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
DecodePQHydraV2T3(D &&downstream) -> DecodePQHydraV2T3<D>;

/**
 * \brief Event set for PicoQuant PicoHarp T3 data stream.
 */
using PQPicoT3Events = EventSet<PQPicoT3Event>;

/**
 * \brief Event set for PicoQuant HydraHarp V1 T3 data stream.
 */
using PQHydraV1T3Events = EventSet<PQHydraV1T3Event>;

/**
 * \brief Event set for PicoQuant HydraHarp V2, MultiHarp, and TimeHarp260 T3
 * data stream.
 */
using PQHydraV2T3Events = EventSet<PQHydraV2T3Event>;

} // namespace flimevt
