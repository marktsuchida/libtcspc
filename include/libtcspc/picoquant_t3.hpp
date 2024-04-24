/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "int_types.hpp"
#include "introspect.hpp"
#include "npint.hpp"
#include "read_integers.hpp"
#include "span.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

// The two T3 formats (pqt3_picoharp300_event and basic_pqt3_event) use
// matching member names for static polymorphism. This allows
// decode_pqt3<PQT3Event> to handle 3 different formats with the same code.

/**
 * \brief Binary record interpretation for PicoHarp 300 T3 Format.
 *
 * \ingroup events-pq
 *
 * RecType 0x00010303.
 */
struct pqt3_picoharp300_event {
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
        return read_u8_at<3>(span(bytes)) >> 4;
    }

    /**
     * \brief Read the difference time if this event is a non-special event.
     */
    [[nodiscard]] auto dtime() const noexcept -> u16np {
        return read_u16le_at<2>(span(bytes)) & 0x0fff_u16np;
    }

    /**
     * \brief Read the nsync counter value if this event is a non-special event
     * or an external marker event.
     */
    [[nodiscard]] auto nsync() const noexcept -> u16np {
        return read_u16le_at<0>(span(bytes));
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
        // 300 T3, but they are in a 4-bit field in T2, and in
        // HydraHarp/Generic T2/T3 the marker bits explicitly range 1-15. Let's
        // just behave consistently here and check the upper limit.
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
     * \brief Make an event representing a non-special (photon) event.
     *
     * \param nsync the nsync count; 0 to 65,535
     *
     * \param channel the channel; 0 to 14
     *
     * \param dtime the difference time; 0 to 4095
     *
     * \return event
     */
    static auto make_nonspecial(u16np nsync, u8np channel, u16np dtime)
        -> pqt3_picoharp300_event {
        if (channel > 14_u8np)
            throw std::invalid_argument(
                "pqt3_picoharp300_event channel must be in the range 0-14");
        return make_from_fields(channel, dtime, nsync);
    }

    /**
     * \brief Make an event representing an nsync overflow.
     *
     * \return event
     */
    static auto make_nsync_overflow() noexcept -> pqt3_picoharp300_event {
        return make_from_fields(15_u8np, 0_u16np, 0_u16np);
    }

    /**
     * \brief Make an event representing an external marker.
     *
     * \param nsync the nsync count; 0 to 65,535
     *
     * \param marker_bits the marker bitmask; 1 to 15 (0 is forbidden)
     *
     * \return event
     */
    static auto make_external_marker(u16np nsync, u8np marker_bits)
        -> pqt3_picoharp300_event {
        if (marker_bits == 0_u8np)
            throw std::invalid_argument(
                "pqt3_picoharp300_event marker_bits must not be zero");
        return make_from_fields(15_u8np, u16np(marker_bits & 0x0f_u8np),
                                nsync);
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pqt3_picoharp300_event const &lhs,
                           pqt3_picoharp300_event const &rhs) noexcept
        -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pqt3_picoharp300_event const &lhs,
                           pqt3_picoharp300_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           pqt3_picoharp300_event const &event)
        -> std::ostream & {
        return stream << "pqt3_picoharp(channel="
                      << unsigned(event.channel().value())
                      << ", dtime=" << event.dtime()
                      << ", nsync=" << event.nsync() << ")";
    }

  private:
    static auto make_from_fields(u8np channel, u16np dtime, u16np nsync)
        -> pqt3_picoharp300_event {
        return pqt3_picoharp300_event{{
            std::byte(u8np(nsync).value()),
            std::byte(u8np(nsync >> 8).value()),
            std::byte(u8np(dtime).value()),
            std::byte(
                ((channel << 4) | (u8np(dtime >> 8) & 0x0f_u8np)).value()),
        }};
    }
};

/**
 * \brief Implementation for binary record interpretation for HydraHarp,
 * MultiHarp, TimeHarp 260, and PicoHarp 330 T3 format.
 *
 * \ingroup events-pq
 *
 * This class is documented to show the available member functions. User code
 * should use `tcspc::pqt3_hydraharpv1_event` or `tcspc::pqt3_generic_event`.
 *
 * \tparam IsNSyncOverflowAlwaysSingle if true, interpret as HydraHarp V1
 * (RecType 0x00010304) format, in which nsync overflow records always indicate
 * a single overflow
 */
template <bool IsNSyncOverflowAlwaysSingle> struct basic_pqt3_event {
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
        return (read_u8_at<3>(span(bytes)) & 0x7f_u8np) >> 1;
    }

    /**
     * \brief Read the difference time if this event is a non-special event.
     */
    [[nodiscard]] auto dtime() const noexcept -> u16np {
        auto const lo6 = u16np(read_u8_at<1>(span(bytes))) >> 2;
        auto const mid8 = u16np(read_u8_at<2>(span(bytes)));
        auto const hi1 = u16np(read_u8_at<3>(span(bytes))) & 1_u16np;
        return lo6 | (mid8 << 6) | (hi1 << 14);
    }

    /**
     * \brief Read the nsync counter value if this event is a non-special event
     * or an external marker event.
     */
    [[nodiscard]] auto nsync() const noexcept -> u16np {
        return read_u16le_at<0>(span(bytes)) & 0x03ff_u16np;
    }

    /**
     * \brief Determine if this event is a special event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return (read_u8_at<3>(span(bytes)) & (1_u8np << 7)) != 0_u8np;
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
     * \brief Make an event representing a non-special (photon) event.
     *
     * \param nsync the nsync count; 0 to 1023
     *
     * \param channel the channel; 0 to 63
     *
     * \param dtime the difference time; 0 to 32767
     *
     * \return event
     */
    static auto make_nonspecial(u16np nsync, u8np channel, u16np dtime)
        -> basic_pqt3_event {
        return make_from_fields(false, channel, dtime, nsync);
    }

    /**
     * \brief Make an event representing an nsync overflow.
     *
     * This overload is only available in \ref pqt3_generic_event.
     *
     * \param count number of overflows; 1 to 1023 (0 is allowed but may not be
     * handled correctly by other readers)
     *
     * \return event
     */
    static auto make_nsync_overflow(u16np count) -> basic_pqt3_event {
        static_assert(
            not IsNSyncOverflowAlwaysSingle,
            "multiple nsync overflow is not available in HydraHarp V1 format");
        return make_from_fields(true, 63_u8np, 0_u16np, count);
    }

    /**
     * \brief Make an event representing a single nsync overflow.
     *
     * \return event;
     */
    static auto make_nsync_overflow() noexcept -> basic_pqt3_event {
        return make_from_fields(true, 63_u8np, 0_u16np, 1_u16np);
    }

    /**
     * \brief Make an event representing an external marker.
     *
     * \param nsync the nsync count; 0 to 1023
     *
     * \param marker_bits the marker bitmask; 1 to 15
     *
     * \return event
     */
    static auto make_external_marker(u16np nsync, u8np marker_bits)
        -> basic_pqt3_event {
        return make_from_fields(true, marker_bits, 0_u16np, nsync);
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(basic_pqt3_event const &lhs,
                           basic_pqt3_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(basic_pqt3_event const &lhs,
                           basic_pqt3_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream, basic_pqt3_event const &event)
        -> std::ostream & {
        static constexpr auto version = IsNSyncOverflowAlwaysSingle ? 1 : 2;
        return stream << "pqt3_hydraharpv" << version
                      << "(special=" << event.is_special()
                      << ", channel=" << unsigned(event.channel().value())
                      << ", dtime=" << event.dtime()
                      << ", nsync=" << event.nsync() << ")";
    }

  private:
    static auto make_from_fields(bool special, u8np channel, u16np dtime,
                                 u16np nsync) -> basic_pqt3_event {
        return basic_pqt3_event{{
            std::byte(u8np(nsync).value()),
            std::byte(
                (u8np(dtime << 2) | (u8np(nsync >> 8) & 0x03_u8np)).value()),
            std::byte(u8np(dtime >> 6).value()),
            std::byte(((u8np(u8(special)) << 7) |
                       ((channel & 0x3f_u8np) << 1) |
                       (u8np(dtime >> 14) & 0x01_u8np))
                          .value()),
        }};
    }
};

/**
 * \brief Binary record interpretation for HydraHarp V1 T3 format.
 *
 * \ingroup events-pq
 *
 * RecType 0x00010304.
 */
using pqt3_hydraharpv1_event = basic_pqt3_event<true>;

/**
 * \brief Binary record interpretation for HydraHarp V2, MultiHarp,
 * TimeHarp 260, and PicoHarp 330 "Generic" T3 format.
 *
 * \ingroup events-pq
 *
 * RecType 0x01010304, 0x00010305, 0x00010306, 0x00010307.
 */
using pqt3_generic_event = basic_pqt3_event<false>;

namespace internal {

// Common implementation for decode_pqt3_*.
// PQT3Event is the binary record event class.
template <typename DataTypes, typename PQT3Event, typename Downstream>
class decode_pqt3 {
    using abstime_type = typename DataTypes::abstime_type;

    abstime_type nsync_base = 0;

    Downstream downstream;

    LIBTCSPC_NOINLINE void issue_warning(char const *message) {
        downstream.handle(warning_event{message});
    }

  public:
    explicit decode_pqt3(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "decode_pqt3");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(PQT3Event const &event) {
        if (event.is_nsync_overflow()) {
            nsync_base += abstime_type(PQT3Event::nsync_overflow_period) *
                          event.nsync_overflow_count().value();
            return downstream.handle(
                time_reached_event<DataTypes>{nsync_base});
        }

        abstime_type const nsync = nsync_base + event.nsync().value();

        if (not event.is_special()) {
            downstream.handle(time_correlated_detection_event<DataTypes>{
                nsync, event.channel().value(), event.dtime().value()});
        } else if (event.is_external_marker()) {
            for_each_set_bit(u32np(event.external_marker_bits()), [&](int b) {
                downstream.handle(marker_event<DataTypes>{nsync, b});
            });
        } else {
            issue_warning("invalid special event encountered");
        }
    }

    void flush() { downstream.flush(); }
};
} // namespace internal

/**
 * \brief Create a processor that decodes PicoQuant PicoHarp 300 T3 events.
 *
 * \ingroup processors-pq
 *
 * \tparam DataTypes data type set specifying `abstime_type`, `channel_type`,
 * and `difftime_type` for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::pqt3_picoharp300_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTypes>`,
 *   `tcspc::time_correlated_detection_event<DataTypes>`,
 *   `tcspc::marker_event<DataTypes>`, `tcspc::warning_event` (warning in the
 *   case of an invalid event)
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto decode_pqt3_picoharp300(Downstream &&downstream) {
    return internal::decode_pqt3<DataTypes, pqt3_picoharp300_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V1 T3 events.
 *
 * \ingroup processors-pq
 *
 * \tparam DataTypes data type set specifying `abstime_type`, `channel_type`,
 * and `difftime_type` for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::pqt3_hydraharpv1_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTypes>`,
 *   `tcspc::time_correlated_detection_event<DataTypes>`,
 *   `tcspc::marker_event<DataTypes>`, `tcspc::warning_event` (warning in the
 *   case of an invalid event)
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto decode_pqt3_hydraharpv1(Downstream &&downstream) {
    return internal::decode_pqt3<DataTypes, pqt3_hydraharpv1_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V2, MultiHarp,
 * TimeHarp 260, and PicoHarp 330 "Generic" T3 events.
 *
 * \ingroup processors-pq
 *
 * \tparam DataTypes data type set specifying `abstime_type`, `channel_type`,
 * and `difftime_type` for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::pqt3_generic_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTypes>`,
 *   `tcspc::time_correlated_detection_event<DataTypes>`,
 *   `tcspc::marker_event<DataTypes>`, `tcspc::warning_event` (warning in the
 *   case of an invalid event)
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto decode_pqt3_generic(Downstream &&downstream) {
    return internal::decode_pqt3<DataTypes, pqt3_generic_event, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
