/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace tcspc {

// PicoQuant raw photon event ("TTTR") formats are documented in the html files
// contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

// Note that code here is written to run on little- or big-endian machines; see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html

// The two T3 formats (pqt3_picoharp_event and pqt3_hydraharp_event) use
// matching member names for static polymorphism. This allows
// decode_pqt3<PQT3Event> to handle 3 different formats with the same code.

/**
 * \brief Binary record interpretation for PicoHarp T3 Format.
 *
 * \ingroup events-device
 *
 * RecType 0x00010303.
 */
struct pqt3_picoharp_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    std::array<std::byte, 4> bytes;

    /**
     * \brief The nsync overflow period of this event type.
     */
    static constexpr std::int32_t nsync_overflow_period = 65536;

    /**
     * \brief Read the channel if this event represents a photon.
     */
    [[nodiscard]] auto channel() const noexcept -> u8np {
        return read_u8(byte_subspan<3, 1>(bytes)) >> 4;
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    [[nodiscard]] auto dtime() const noexcept -> u16np {
        return read_u16le(byte_subspan<2, 2>(bytes)) & 0x0fff_u16np;
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    [[nodiscard]] auto nsync() const noexcept -> u16np {
        return read_u16le(byte_subspan<0, 2>(bytes));
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return channel() == 15_u8np;
    }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    [[nodiscard]] auto is_nsync_overflow() const noexcept -> bool {
        return is_special() && dtime() == 0_u16np;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    [[nodiscard]] static auto nsync_overflow_count() noexcept -> u16np {
        return 1_u16np;
    }

    /**
     * \brief Determine if this event represents markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && dtime() != 0_u16np;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto external_marker_bits() const noexcept -> u16np {
        return dtime();
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pqt3_picoharp_event const &lhs,
                           pqt3_picoharp_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pqt3_picoharp_event const &lhs,
                           pqt3_picoharp_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, pqt3_picoharp_event const &e)
        -> std::ostream & {
        return strm << "pqt3_picoharp(channel=" << e.channel()
                    << ", dtime=" << e.dtime() << ", nsync=" << e.nsync()
                    << ")";
    }
};

/**
 * \brief Implementation for binary record interpretation for HydraHarp,
 * MultiHarp, and TimeHarp260 T3 format.
 *
 * \ingroup events-device
 *
 * User code should use \ref pqt3_hydraharpv1_event or \ref
 * pqt3_hydraharpv2_event.
 *
 * \tparam IsNSyncOverflowAlwaysSingle if true, interpret as HydraHarp V1
 * (RecType 0x00010304) format, in which nsync overflow records always indicate
 * a single overflow
 */
template <bool IsNSyncOverflowAlwaysSingle> struct pqt3_hydraharp_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    std::array<std::byte, 4> bytes;

    /**
     * \brief The nsync overflow period of this event type.
     */
    static constexpr std::int32_t nsync_overflow_period = 1024;

    /**
     * \brief Read the channel if this event represents a photon.
     */
    [[nodiscard]] auto channel() const noexcept -> u8np {
        return (read_u8(byte_subspan<3, 1>(bytes)) & 0x7f_u8np) >> 1;
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    [[nodiscard]] auto dtime() const noexcept -> u16np {
        auto const lo6 = u16np(read_u8(byte_subspan<1, 1>(bytes))) >> 2;
        auto const mid8 = u16np(read_u8(byte_subspan<2, 1>(bytes)));
        auto const hi1 = u16np(read_u8(byte_subspan<3, 1>(bytes))) & 1_u16np;
        return lo6 | (mid8 << 6) | (hi1 << 14);
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    [[nodiscard]] auto nsync() const noexcept -> u16np {
        return read_u16le(byte_subspan<0, 2>(bytes)) & 0x03ff_u16np;
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 7)) != 0_u8np;
    }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    [[nodiscard]] auto is_nsync_overflow() const noexcept -> bool {
        return is_special() && channel() == 63_u8np;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    [[nodiscard]] auto nsync_overflow_count() const noexcept -> u16np {
        if (IsNSyncOverflowAlwaysSingle || nsync() == 0_u16np)
            return 1_u16np;
        return nsync();
    }

    /**
     * \brief Determine if this event represents markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && channel() != 63_u8np;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto external_marker_bits() const noexcept -> u8np {
        return channel();
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pqt3_hydraharp_event const &lhs,
                           pqt3_hydraharp_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pqt3_hydraharp_event const &lhs,
                           pqt3_hydraharp_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, pqt3_hydraharp_event const &e)
        -> std::ostream & {
        auto version = IsNSyncOverflowAlwaysSingle ? 1 : 2;
        return strm << "pqt3_hydraharpv" << version
                    << "(special=" << e.is_special()
                    << ", channel=" << e.channel() << ", dtime=" << e.dtime()
                    << ", nsync=" << e.nsync() << ")";
    }
};

/**
 * \brief Binary record interpretation for HydraHarp V1 T3 format.
 *
 * \ingroup events-device
 */
using pqt3_hydraharpv1_event = pqt3_hydraharp_event<true>;

/**
 * \brief Binary record interpretation for HydraHarp V2, MultiHarp, and
 * TimeHarp260 T3 format.
 *
 * \ingroup events-device
 */
using pqt3_hydraharpv2_event = pqt3_hydraharp_event<false>;

namespace internal {

// Common implementation for decode_pqt3_*.
// PQT3Event is the binary record event class.
template <typename DataTraits, typename PQT3Event, typename Downstream>
class decode_pqt3 {
    using abstime_type = typename DataTraits::abstime_type;

    abstime_type nsync_base = 0;

    Downstream downstream;

  public:
    explicit decode_pqt3(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void handle_event(PQT3Event const &event) noexcept {
        if (event.is_nsync_overflow()) {
            nsync_base += abstime_type(PQT3Event::nsync_overflow_period) *
                          event.nsync_overflow_count();
            return downstream.handle_event(
                time_reached_event<DataTraits>{{nsync_base}});
        }

        abstime_type const nsync = nsync_base + event.nsync();

        if (event.is_external_marker()) {
            auto bits = u32np(event.external_marker_bits());
            while (bits != 0_u32np) {
                downstream.handle_event(marker_event<DataTraits>{
                    {{nsync}, count_trailing_zeros_32(bits)}});
                bits = bits & (bits - 1_u32np); // Clear the handled bit
            }
            return;
        }

        downstream.handle_event(time_correlated_detection_event<DataTraits>{
            {{nsync}, event.channel()}, event.dtime()});
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};
} // namespace internal

/**
 * \brief Create a processor that decodes PicoQuant PicoHarp T3 events.
 *
 * \ingroup processors-decode
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return decode-pqt3-picoharp processor
 */
template <typename DataTraits, typename Downstream>
auto decode_pqt3_picoharp(Downstream &&downstream) {
    return internal::decode_pqt3<DataTraits, pqt3_picoharp_event, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V1 T3 events.
 *
 * \ingroup processors-decode
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return decode-pqt3-hydraharpv1 processor
 */
template <typename DataTraits, typename Downstream>
auto decode_pqt3_hydraharpv1(Downstream &&downstream) {
    return internal::decode_pqt3<DataTraits, pqt3_hydraharpv1_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V2, MultiHarp,
 * and TimeHarp260 T3 events.
 *
 * \ingroup processors-decode
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return decode-pqt3-hydraharpv2 processor
 */
template <typename DataTraits, typename Downstream>
auto decode_pqt3_hydraharpv2(Downstream &&downstream) {
    return internal::decode_pqt3<DataTraits, pqt3_hydraharpv2_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
