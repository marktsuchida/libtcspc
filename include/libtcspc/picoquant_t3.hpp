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
#include <stdexcept>
#include <utility>

// When editing this file, maintain the partial symmetry with picoquant_t2.hpp.

namespace tcspc {

// PicoQuant raw time tag event ("TTTR") formats are documented in the html
// files contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

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
     * \brief Read the channel if this event is a non-special event.
     */
    [[nodiscard]] auto channel() const noexcept -> u8np {
        return read_u8(byte_subspan<3, 1>(bytes)) >> 4;
    }

    /**
     * \brief Read the difference time if this event is a non-special event.
     */
    [[nodiscard]] auto dtime() const noexcept -> u16np {
        return read_u16le(byte_subspan<2, 2>(bytes)) & 0x0fff_u16np;
    }

    /**
     * \brief Read the nsync counter value if this event is a non-special event
     * or an external marker event.
     */
    [[nodiscard]] auto nsync() const noexcept -> u16np {
        return read_u16le(byte_subspan<0, 2>(bytes));
    }

    /**
     * \brief Determine if this event is a special event.
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
     * \brief Determine if this event represents external markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        // Vendor docs do not specifically say markers are 4 bits in PicoHarp
        // T3, but they are in a 4-bit field in T2, and in HydraHarp T2/T3 the
        // marker bits explicitly range 1-15. Let's just behave consistently
        // here and check the upper limit.
        return is_special() && dtime() > 0_u16np && dtime() <= 15_u16np;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents external
     * markers.
     */
    [[nodiscard]] auto external_marker_bits() const noexcept -> u16np {
        return dtime();
    }

    /**
     * \brief Set this event to represent a non-special (photon) event.
     *
     * \param nsync the nsync count; 0 to 65535
     *
     * \param channel the channel; 0 to 14
     *
     * \param dtime the difference time; 0 to 4095
     *
     * \return \c *this
     */
    auto assign_nonspecial(u16np nsync, u8np channel, u16np dtime)
        -> pqt3_picoharp_event & {
        if (channel > 14_u8np)
            throw std::invalid_argument(
                "pqt3_picoharp_event channel must be in the range 0-14");
        return assign_fields(channel, dtime, nsync);
    }

    /**
     * \brief Set this event to represent an nsync overflow.
     *
     * \return \c *this
     */
    auto assign_nsync_overflow() noexcept -> pqt3_picoharp_event & {
        return assign_fields(15_u8np, 0_u16np, 0_u16np);
    }

    /**
     * \brief Set this event to represent an external marker.
     *
     * \param nsync the nsync count; 0 to 65535
     *
     * \param marker_bits the marker bitmask; 1 to 15 (0 is forbidden)
     *
     * \return \c *this
     */
    auto assign_external_marker(u16np nsync, u8np marker_bits)
        -> pqt3_picoharp_event & {
        if (marker_bits == 0_u8np)
            throw std::invalid_argument(
                "pqt3_picoharp_event marker_bits must not be zero");
        return assign_fields(15_u8np, u16np(marker_bits & 0x0f_u8np), nsync);
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
    friend auto operator<<(std::ostream &stream,
                           pqt3_picoharp_event const &event)
        -> std::ostream & {
        return stream << "pqt3_picoharp(channel="
                      << unsigned(event.channel().value())
                      << ", dtime=" << event.dtime()
                      << ", nsync=" << event.nsync() << ")";
    }

  private:
    auto assign_fields(u8np channel, u16np dtime, u16np nsync)
        -> pqt3_picoharp_event & {
        bytes[3] = std::byte(
            ((channel << 4) | (u8np(dtime >> 8) & 0x0f_u8np)).value());
        bytes[2] = std::byte(u8np(dtime).value());
        bytes[1] = std::byte(u8np(nsync >> 8).value());
        bytes[0] = std::byte(u8np(nsync).value());
        return *this;
    }
};

/**
 * \brief Implementation for binary record interpretation for HydraHarp,
 * MultiHarp, and TimeHarp260 T3 format.
 *
 * \ingroup events-device
 *
 * This class is documented to show the available member functions. User code
 * should use \ref pqt3_hydraharpv1_event or \ref pqt3_hydraharpv2_event.
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
     * \brief Read the channel if this event is a non-special event.
     */
    [[nodiscard]] auto channel() const noexcept -> u8np {
        return (read_u8(byte_subspan<3, 1>(bytes)) & 0x7f_u8np) >> 1;
    }

    /**
     * \brief Read the difference time if this event is a non-special event.
     */
    [[nodiscard]] auto dtime() const noexcept -> u16np {
        auto const lo6 = u16np(read_u8(byte_subspan<1, 1>(bytes))) >> 2;
        auto const mid8 = u16np(read_u8(byte_subspan<2, 1>(bytes)));
        auto const hi1 = u16np(read_u8(byte_subspan<3, 1>(bytes))) & 1_u16np;
        return lo6 | (mid8 << 6) | (hi1 << 14);
    }

    /**
     * \brief Read the nsync counter value if this event is a non-special event
     * or an external marker event.
     */
    [[nodiscard]] auto nsync() const noexcept -> u16np {
        return read_u16le(byte_subspan<0, 2>(bytes)) & 0x03ff_u16np;
    }

    /**
     * \brief Determine if this event is a special event.
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
        if (IsNSyncOverflowAlwaysSingle)
            return 1_u16np;
        return nsync();
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

    /**
     * \brief Set this event to represent a non-special (photon) event.
     *
     * \param nsync the nsync count; 0 to 1023
     *
     * \param channel the channel; 0 to 63
     *
     * \param dtime the difference time; 0 to 32767
     *
     * \return \c *this
     */
    auto assign_nonspecial(u16np nsync, u8np channel, u16np dtime)
        -> pqt3_hydraharp_event & {
        return assign_fields(false, channel, dtime, nsync);
    }

    /**
     * \brief Set this event to represent an nsync overflow.
     *
     * This overload is only available in \ref pqt3_hydraharpv2_event.
     *
     * \param count number of overflows; 1 to 1023 (0 is allowed but may not be
     * handled correctly by other readers)
     *
     * \return \c *this
     */
    auto assign_nsync_overflow(u16np count) -> pqt3_hydraharp_event & {
        static_assert(
            not IsNSyncOverflowAlwaysSingle,
            "multiple nsync overflow is not available in HydraHarp V1 format");
        return assign_fields(true, 63_u8np, 0_u16np, count);
    }

    /**
     * \brief Set this event to represent a single nsync overflow.
     *
     * \return \c *this;
     */
    auto assign_nsync_overflow() noexcept -> pqt3_hydraharp_event & {
        return assign_fields(true, 63_u8np, 0_u16np, 1_u16np);
    }

    /**
     * \brief Set this event to represent an external marker.
     *
     * \param nsync the nsync count; 0 to 1023
     *
     * \param marker_bits the marker bitmask; 1 to 15
     *
     * \return \c *this
     */
    auto assign_external_marker(u16np nsync, u8np marker_bits)
        -> pqt3_hydraharp_event & {
        return assign_fields(true, marker_bits, 0_u16np, nsync);
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
    friend auto operator<<(std::ostream &stream,
                           pqt3_hydraharp_event const &event)
        -> std::ostream & {
        static constexpr auto version = IsNSyncOverflowAlwaysSingle ? 1 : 2;
        return stream << "pqt3_hydraharpv" << version
                      << "(special=" << event.is_special()
                      << ", channel=" << unsigned(event.channel().value())
                      << ", dtime=" << event.dtime()
                      << ", nsync=" << event.nsync() << ")";
    }

  private:
    auto assign_fields(bool special, u8np channel, u16np dtime, u16np nsync)
        -> pqt3_hydraharp_event & {
        bytes[3] =
            std::byte(((u8np(special) << 7) | ((channel & 0x3f_u8np) << 1) |
                       (u8np(dtime >> 14) & 0x01_u8np))
                          .value());
        bytes[2] = std::byte(u8np(dtime >> 6).value());
        bytes[1] = std::byte(
            (u8np(dtime << 2) | (u8np(nsync >> 8) & 0x03_u8np)).value());
        bytes[0] = std::byte(u8np(nsync).value());
        return *this;
    }
};

/**
 * \brief Binary record interpretation for HydraHarp V1 T3 format.
 *
 * \ingroup events-device
 *
 * RecType 0x00010304.
 */
using pqt3_hydraharpv1_event = pqt3_hydraharp_event<true>;

/**
 * \brief Binary record interpretation for HydraHarp V2, MultiHarp, and
 * TimeHarp260 T3 format.
 *
 * \ingroup events-device
 *
 * RecType 01010304, 00010305, 00010306, 00010307.
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

    LIBTCSPC_NOINLINE void issue_warning(char const *message) {
        downstream.handle(warning_event{message});
    }

  public:
    explicit decode_pqt3(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void handle(PQT3Event const &event) {
        if (event.is_nsync_overflow()) {
            nsync_base += abstime_type(PQT3Event::nsync_overflow_period) *
                          event.nsync_overflow_count().value();
            return downstream.handle(
                time_reached_event<DataTraits>{{nsync_base}});
        }

        abstime_type const nsync = nsync_base + event.nsync().value();

        if (not event.is_special()) {
            downstream.handle(time_correlated_detection_event<DataTraits>{
                {{nsync}, event.channel().value()}, event.dtime().value()});
        } else if (event.is_external_marker()) {
            for_each_set_bit(u32np(event.external_marker_bits()), [&](int b) {
                downstream.handle(marker_event<DataTraits>{{{nsync}, b}});
            });
        } else {
            issue_warning("invalid special event encountered");
        }
    }

    void flush() { downstream.flush(); }
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
 * \param downstream downstream processor
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
 * \param downstream downstream processor
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
 * \param downstream downstream processor
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
