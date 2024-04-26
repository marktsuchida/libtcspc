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

// When editing this file, maintain the partial symmetry with picoquant_t3.hpp.

namespace tcspc {

// PicoQuant raw time tag event ("TTTR") formats are documented in the html
// files contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

// The two T2 formats (pqt2_picoharp300_event and basic_pqt2_event) use
// matching member names for static polymorphism. This allows
// decode_pqt2<PQT2Event> to handle 3 different formats with the same code.

/**
 * \brief Binary record interpretation for PicoHarp 300 T2 Format.
 *
 * \ingroup events-pq
 *
 * RecType 0x00010203.
 */
struct pqt2_picoharp300_event {
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
        return read_u8_at<3>(span(bytes)) >> 4;
    }

    /**
     * \brief Read the time tag if this event is a non-special event (not
     * external marker event).
     */
    [[nodiscard]] auto timetag() const noexcept -> u32np {
        return read_u32le_at<0>(span(bytes)) & 0x0fff'ffff_u32np;
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

    /**
     * \brief Make an event representing a non-special (photon) event.
     *
     * \param timetag the time tag; 0 to 268,435,455
     *
     * \param channel the channel; 0 to 14
     *
     * \return event
     */
    static auto make_nonspecial(u32np timetag, u8np channel)
        -> pqt2_picoharp300_event {
        if (channel > 14_u8np)
            throw std::invalid_argument(
                "pqt2_picoharp300_event channel must be in the range 0-14");
        return make_from_fields(channel, timetag);
    }

    /**
     * \brief Make an event representing an time tag overflow.
     *
     * \return event
     */
    static auto make_timetag_overflow() noexcept -> pqt2_picoharp300_event {
        return make_from_fields(15_u8np, 0_u32np);
    }

    /**
     * \brief Make an event representing an external marker.
     *
     * \param timetag the time tag; 0 to 268,435,455; the lower 4 bits are
     * discarded
     *
     * \param marker_bits the marker bitmask; 1 to 15 (0 is forbidden)
     *
     * \return event
     */
    static auto make_external_marker(u32np timetag, u8np marker_bits)
        -> pqt2_picoharp300_event {
        if (marker_bits == 0_u8np)
            throw std::invalid_argument(
                "pqt2_picoharp300_event marker_bits must not be zero");
        return make_from_fields(15_u8np,
                                (timetag & ~0x0f_u32np) |
                                    (u32np(marker_bits) & 0x0f_u32np));
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pqt2_picoharp300_event const &lhs,
                           pqt2_picoharp300_event const &rhs) noexcept
        -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pqt2_picoharp300_event const &lhs,
                           pqt2_picoharp300_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream,
                           pqt2_picoharp300_event const &event)
        -> std::ostream & {
        return stream << "pqt2_picoharp(channel="
                      << unsigned(event.channel().value())
                      << ", timetag=" << event.timetag() << ")";
    }

  private:
    static auto make_from_fields(u8np channel, u32np timetag)
        -> pqt2_picoharp300_event {
        return pqt2_picoharp300_event{{
            std::byte(u8np(timetag).value()),
            std::byte(u8np(timetag >> 8).value()),
            std::byte(u8np(timetag >> 16).value()),
            std::byte(
                ((channel << 4) | (u8np(timetag >> 24) & 0x0f_u8np)).value()),
        }};
    }
};

/**
 * \brief Implementation for binary record interpretation for HydraHarp,
 * MultiHarp, TimeHarp 260, and PicoHarp 330 T2 format.
 *
 * \ingroup events-pq
 *
 * This class is documented to show the available member functions. User code
 * should use `tcspc::pqt2_hydraharpv1_event` or `tcspc::pqt2_generic_event`.
 *
 * \tparam OverflowPeriod the time tag overflow period
 *
 * \tparam IsOverflowAlwaysSingle if true, time tag overflow records always
 * indicate a single overflow
 */
template <std::int32_t OverflowPeriod, bool IsOverflowAlwaysSingle>
struct basic_pqt2_event {
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
        return (read_u8_at<3>(span(bytes)) & 0x7f_u8np) >> 1;
    }

    /**
     * \brief Read the time tag if this event is a non-special event.
     */
    [[nodiscard]] auto timetag() const noexcept -> u32np {
        return read_u32le_at<0>(span(bytes)) & 0x01ff'ffff_u32np;
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
        return (read_u8_at<3>(span(bytes)) & (1_u8np << 7)) != 0_u8np;
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

    /**
     * \brief Make an event representing a non-special (photon) event.
     *
     * \param timetag the time tag; 0 to 33,554,431
     *
     * \param channel the channel; 0 to 63
     *
     * \return event
     */
    static auto make_nonspecial(u32np timetag, u8np channel)
        -> basic_pqt2_event {
        return make_from_fields(false, channel, timetag);
    }

    /**
     * \brief Make an event representing an time tag overflow.
     *
     * This overload is only available in \ref pqt2_generic_event.
     *
     * \param count number of overflows; 1 to 33,554,431 (0 is allowed but may
     * not be handled correctly by other readers)
     *
     * \return event
     */
    static auto make_timetag_overflow(u32np count) -> basic_pqt2_event {
        static_assert(
            not IsOverflowAlwaysSingle,
            "multiple time tag overflow is not available in HydraHarp V1 format");
        return make_from_fields(true, 63_u8np, count);
    }

    /**
     * \brief Make an event representing a single time tag overflow.
     *
     * \return event
     */
    static auto make_timetag_overflow() noexcept -> basic_pqt2_event {
        return make_from_fields(true, 63_u8np, 1_u32np);
    }

    /**
     * \brief Make an event representing a sync event.
     *
     * \param timetag the time tag; 0 to 33,554,431
     *
     * \return event
     */
    static auto make_sync(u32np timetag) noexcept -> basic_pqt2_event {
        return make_from_fields(true, 0_u8np, timetag);
    }

    /**
     * \brief Make an event representing an external marker.
     *
     * \param timetag the time tag; 0 to 33,554,431
     *
     * \param marker_bits the marker bitmask; 1 to 15 (0 is forbidden)
     *
     * \return event
     */
    static auto make_external_marker(u32np timetag, u8np marker_bits)
        -> basic_pqt2_event {
        if (marker_bits == 0_u8np || (marker_bits & ~0x0f_u8np) != 0_u8np)
            throw std::invalid_argument(
                "basic_pqt2_event marker_bits must be in range 1-15");
        return make_from_fields(true, marker_bits & 0x3f_u8np, timetag);
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(basic_pqt2_event const &lhs,
                           basic_pqt2_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(basic_pqt2_event const &lhs,
                           basic_pqt2_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream, basic_pqt2_event const &event)
        -> std::ostream & {
        static constexpr auto version = IsOverflowAlwaysSingle ? 1 : 2;
        return stream << "pqt2_hydraharpv" << version
                      << "(special=" << event.is_special()
                      << ", channel=" << unsigned(event.channel().value())
                      << ", timetag=" << event.timetag() << ")";
    }

  private:
    static auto make_from_fields(bool special, u8np channel, u32np timetag)
        -> basic_pqt2_event {
        return basic_pqt2_event{{
            std::byte(u8np(timetag).value()),
            std::byte(u8np(timetag >> 8).value()),
            std::byte(u8np(timetag >> 16).value()),
            std::byte(((u8np(u8(special)) << 7) |
                       ((channel & 0x3f_u8np) << 1) |
                       (u8np(timetag >> 24) & 0x01_u8np))
                          .value()),
        }};
    }
};

/**
 * \brief Binary record interpretation for HydraHarp V1 T2 format.
 *
 * \ingroup events-pq
 *
 * RecType 0x00010204.
 */
using pqt2_hydraharpv1_event = basic_pqt2_event<33552000, true>;

/**
 * \brief Binary record interpretation for HydraHarp V2, MultiHarp,
 * TimeHarp 260, and PicoHarp 330 "Generic" T2 format.
 *
 * \ingroup events-pq
 *
 * RecType 0x01010204, 0x00010205, 0x00010206, 0x00010207.
 */
using pqt2_generic_event = basic_pqt2_event<33554432, false>;

namespace internal {

// Common implementation for decode_pqt2_*.
// PQT2Event is the binary record event class.
template <typename DataTypes, typename PQT2Event, typename Downstream>
class decode_pqt2 {
    using abstime_type = typename DataTypes::abstime_type;

    abstime_type timetag_base = 0;

    Downstream downstream;

    LIBTCSPC_NOINLINE void issue_warning(char const *message) {
        downstream.handle(warning_event{message});
    }

  public:
    explicit decode_pqt2(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "decode_pqt2");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(PQT2Event const &event) {
        if (event.is_timetag_overflow()) {
            timetag_base += abstime_type(PQT2Event::overflow_period) *
                            event.timetag_overflow_count().value();
            return downstream.handle(
                time_reached_event<DataTypes>{timetag_base});
        }

        // In the case where the overflow period is smaller than one plus the
        // maximum representable time tag (PicoHarp 300 and HydraHarp V1), any
        // invalid time tags will be caught when (externally) checking for
        // monotonicity. So we do not check here.

        if (not event.is_special() || event.is_sync_event()) {
            abstime_type const timetag =
                timetag_base + event.timetag().value();
            downstream.handle(detection_event<DataTypes>{
                timetag, event.is_special() ? -1 : event.channel().value()});
        } else if (event.is_external_marker()) {
            abstime_type const timetag =
                timetag_base + event.external_marker_timetag().value();
            for_each_set_bit(u32np(event.external_marker_bits()), [&](int b) {
                downstream.handle(marker_event<DataTypes>{timetag, b});
            });
        } else {
            issue_warning("invalid special event encountered");
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that decodes PicoQuant PicoHarp 300 T2 events.
 *
 * \ingroup processors-pq
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 * for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::pqt2_picoharp300_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTypes>`,
 *   `tcspc::detection_event<DataTypes>`, `tcspc::marker_event<DataTypes>`,
 *   `tcspc::warning_event` (warning in the case of an invalid event)
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto decode_pqt2_picoharp300(Downstream &&downstream) {
    return internal::decode_pqt2<DataTypes, pqt2_picoharp300_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V1 T2 events.
 *
 * \ingroup processors-pq
 *
 * Sync events (edges detected on the sync channel) are reported as detection
 * events on channel -1.
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 * for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::pqt2_hydraharpv1_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTypes>`,
 *   `tcspc::detection_event<DataTypes>`, `tcspc::marker_event<DataTypes>`,
 *   `tcspc::warning_event` (warning in the case of an invalid event)
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto decode_pqt2_hydraharpv1(Downstream &&downstream) {
    return internal::decode_pqt2<DataTypes, pqt2_hydraharpv1_event,
                                 Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V2, MultiHarp,
 * TimeHarp 260, and PicoHarp 330 "Generic" T2 events.
 *
 * \ingroup processors-pq
 *
 * Sync events (edges detected on the sync channel) are reported as detection
 * events on channel -1.
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 * for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::pqt2_generic_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTypes>`,
 *   `tcspc::detection_event<DataTypes>`, `tcspc::marker_event<DataTypes>`,
 *   `tcspc::warning_event` (warning in the case of an invalid event)
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto decode_pqt2_generic(Downstream &&downstream) {
    return internal::decode_pqt2<DataTypes, pqt2_generic_event, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
