/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ostream>
#include <utility>

// When editing this file, maintain the partial symmetry with picoquant_t3.hpp.

namespace tcspc {

// PicoQuant raw time tag event ("TTTR") formats are documented in the html
// files contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

// The two T2 formats (pqt2_picoharp_event and pqt2_hydraharp_event) use
// matching member names for static polymorphism. This allows
// decode_pqt2<PQT2Event> to handle 3 different formats with the same code.

/**
 * \brief Binary record interpretation for PicoHarp T2 Format.
 *
 * \ingroup events-device
 *
 * RecType 0x00010203.
 */
struct pqt2_picoharp_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    std::array<std::byte, 4> bytes;

    /**
     * \brief The time tag overflow period of this event type.
     *
     * Note that this does \e not equal one plus the maximum representable
     * value of the time tag field.
     */
    static constexpr std::int32_t overflow_period = 210698240;

    /**
     * \brief Read the channel if this event is a non-special event.
     */
    [[nodiscard]] auto channel() const noexcept -> u8np {
        return read_u8(byte_subspan<3, 1>(bytes)) >> 4;
    }

    /**
     * \brief Read the time tag if this event is a non-special event (not
     * external marker event).
     */
    [[nodiscard]] auto timetag() const noexcept -> u32np {
        return read_u32le(bytes) & 0x0fff'ffff_u32np;
    }

    /**
     * \brief Read the time tag if this event is an external marker event.
     *
     * The low 4 bits are zeroed to remove the marker bits.
     */
    [[nodiscard]] auto external_marker_timetag() const noexcept -> u32np {
        // For markers, the low 4 bits of the time tag are used to store the
        // marker bits, giving markers 1/16 the time resolution (the actual
        // time resolution for markers is even lower, in the 10s of ns range).
        // Avoid leaving the marker bits in the timestamp.
        return timetag() & ~0x0f_u32np;
    }

    /**
     * \brief Determine if this event is a special event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return channel() == 15_u8np;
    }

    /**
     * \brief Determine if this event represents a time tag overflow.
     */
    [[nodiscard]] auto is_timetag_overflow() const noexcept -> bool {
        return is_special() && (timetag() & 0x0f_u32np) == 0_u32np;
    }

    /**
     * \brief Read the time tag overflow count if this event represents a time
     * tag overflow.
     */
    [[nodiscard]] static auto timetag_overflow_count() noexcept -> u32np {
        return 1_u32np;
    }

    /**
     * \brief Determine if this event represents a sync event.
     */
    [[nodiscard]] static auto is_sync_event() noexcept -> bool {
        return false;
    }

    /**
     * \brief Determine if this event represents external markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && (timetag() & 0x0f_u32np) != 0_u32np;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents external
     * markers.
     */
    [[nodiscard]] auto external_marker_bits() const noexcept -> u8np {
        return u8np(timetag()) & 0x0f_u8np;
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pqt2_picoharp_event const &lhs,
                           pqt2_picoharp_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pqt2_picoharp_event const &lhs,
                           pqt2_picoharp_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           pqt2_picoharp_event const &event)
        -> std::ostream & {
        return stream << "pqt2_picoharp(channel="
                      << unsigned(event.channel().value())
                      << ", timetag=" << event.timetag() << ")";
    }
};

/**
 * \brief Implementation for binary record interpretation for HydraHarp,
 * MultiHarp, and TimeHarp260 T2 format.
 *
 * \ingroup events-device
 *
 * This class is documented to show the available member functions. User code
 * should use \ref pqt2_hydraharpv1_event or \ref pqt2_hydraharpv2_event.
 *
 * \tparam OverflowPeriod the time tag overflow period
 *
 * \tparam IsOverflowAlwaysSingle if true, time tag overflow records always
 * indicate a single overflow
 */
template <std::int32_t OverflowPeriod, bool IsOverflowAlwaysSingle>
struct pqt2_hydraharp_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    std::array<std::byte, 4> bytes;

    /**
     * \brief The time tag overflow period of this event type.
     *
     * Note that this may not equal one plus the maximum representable value of
     * the time tag field.
     */
    static constexpr std::int32_t overflow_period = OverflowPeriod;

    /**
     * \brief Read the channel if this event is a non-special event.
     */
    [[nodiscard]] auto channel() const noexcept -> u8np {
        return (read_u8(byte_subspan<3, 1>(bytes)) & 0x7f_u8np) >> 1;
    }

    /**
     * \brief Read the time tag if this event is a non-special event.
     */
    [[nodiscard]] auto timetag() const noexcept -> u32np {
        return read_u32le(bytes) & 0x01ff'ffff_u32np;
    }

    /**
     * \brief Read the time tag if this event is an external marker event.
     */
    [[nodiscard]] auto external_marker_timetag() const noexcept -> u32np {
        return timetag();
    }

    /**
     * \brief Determine if this event is a special event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 7)) != 0_u8np;
    }

    /**
     * \brief Determine if this event represents a time tag overflow.
     */
    [[nodiscard]] auto is_timetag_overflow() const noexcept -> bool {
        return is_special() && channel() == 63_u8np;
    }

    /**
     * \brief Read the time tag overflow count if this event represents a time
     * tag overflow.
     */
    [[nodiscard]] auto timetag_overflow_count() const noexcept -> u32np {
        if (IsOverflowAlwaysSingle)
            return 1_u32np;
        return timetag();
    }

    /**
     * \brief Determine if this event represents a sync event.
     */
    [[nodiscard]] auto is_sync_event() const noexcept -> bool {
        return is_special() && channel() == 0_u8np;
    }

    /**
     * \brief Determine if this event represents external markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && channel() > 0_u8np && channel() <= 15_u8np;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents external
     * markers.
     */
    [[nodiscard]] auto external_marker_bits() const noexcept -> u8np {
        return channel();
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pqt2_hydraharp_event const &lhs,
                           pqt2_hydraharp_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pqt2_hydraharp_event const &lhs,
                           pqt2_hydraharp_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           pqt2_hydraharp_event const &event)
        -> std::ostream & {
        static constexpr auto version = IsOverflowAlwaysSingle ? 1 : 2;
        return stream << "pqt2_hydraharpv" << version
                      << "(special=" << event.is_special()
                      << ", channel=" << unsigned(event.channel().value())
                      << ", timetag=" << event.timetag() << ")";
    }
};

/**
 * \brief Binary record interpretation for HydraHarp V1 T2 format.
 *
 * \ingroup events-device
 *
 * RecType 0x00010204.
 */
using pqt2_hydraharpv1_event = pqt2_hydraharp_event<33552000, true>;

/**
 * \brief Binary record interpretation for HydraHarp V2, MultiHarp, and
 * TimeHarp260 T2 format.
 *
 * \ingroup events-device
 *
 * RecType 01010204, 01010205, 01010206, 01010207.
 */
using pqt2_hydraharpv2_event = pqt2_hydraharp_event<33554432, false>;

namespace internal {

// Common implementation for decode_pqt2_*.
// PQT2Event is the binary record event class.
template <typename DataTraits, typename PQT2Event, typename Downstream>
class decode_pqt2 {
    using abstime_type = typename DataTraits::abstime_type;

    abstime_type timetag_base = 0;

    Downstream downstream;

    LIBTCSPC_NOINLINE void issue_warning(char const *message) noexcept {
        downstream.handle_event(warning_event{message});
    }

  public:
    explicit decode_pqt2(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void handle_event(PQT2Event const &event) noexcept {
        if (event.is_timetag_overflow()) {
            timetag_base += abstime_type(PQT2Event::overflow_period) *
                            event.timetag_overflow_count().value();
            return downstream.handle_event(
                time_reached_event<DataTraits>{{timetag_base}});
        }

        // In the case where the overflow period is smaller than one plus the
        // maximum representable time tag (PicoHarp, and HydraHarp V1), any
        // invalid time tags will be caught when (externally) checking for
        // monotonicity. So we do not check here.

        if (not event.is_special() || event.is_sync_event()) {
            abstime_type const timetag =
                timetag_base + event.timetag().value();
            downstream.handle_event(detection_event<DataTraits>{
                {{timetag},
                 event.is_special() ? -1 : event.channel().value()}});
        } else if (event.is_external_marker()) {
            abstime_type const timetag =
                timetag_base + event.external_marker_timetag().value();
            auto bits = u32np(event.external_marker_bits());
            while (bits != 0_u32np) {
                downstream.handle_event(marker_event<DataTraits>{
                    {{timetag}, count_trailing_zeros_32(bits)}});
                bits = bits & (bits - 1_u32np); // Clear the handled bit
            }
        } else {
            issue_warning("invalid special event encountered");
        }
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that decodes PicoQuant PicoHarp T2 events.
 *
 * \ingroup processors-decode
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return decode-pqt2-picoharp processor
 */
template <typename DataTraits, typename Downstream>
auto decode_pqt2_picoharp(Downstream &&downstream) {
    return internal::decode_pqt2<DataTraits, pqt2_picoharp_event, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V1 T2 events.
 *
 * \ingroup processors-decode
 *
 * Sync events (edges detected on the sync channel) are reported as detection
 * events on channel -1.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return decode-pqt2-hydraharpv1 processor
 */
template <typename DataTraits, typename Downstream>
auto decode_pqt2_hydraharpv1(Downstream &&downstream) {
    return internal::decode_pqt2<DataTraits, pqt2_hydraharpv1_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V2, MultiHarp,
 * and TimeHarp260 T2 events.
 *
 * \ingroup processors-decode
 *
 * Sync events (edges detected on the sync channel) are reported as detection
 * events on channel -1.
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return decode-pqt2-hydraharpv2 processor
 */
template <typename DataTraits, typename Downstream>
auto decode_pqt2_hydraharpv2(Downstream &&downstream) {
    return internal::decode_pqt2<DataTraits, pqt2_hydraharpv2_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
