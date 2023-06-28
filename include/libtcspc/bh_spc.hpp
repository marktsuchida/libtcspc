/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "read_bytes.hpp"
#include "span.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace tcspc {

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
struct bh_spc_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    std::array<std::byte, 4> bytes;

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr macrotime macrotime_overflow_period = 1 << 12;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto adc_value() const noexcept -> std::uint16_t {
        return read_u16le(byte_subspan<2, 2>(bytes)) & 0x0fffu;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto routing_signals() const noexcept -> std::uint8_t {
        // The documentation somewhat confusingly says that these bits are
        // "inverted", but what they mean is that the TTL inputs are active
        // low. The bits in the FIFO data are not inverted.
        return read_u8(byte_subspan<1, 1>(bytes)) >> 4;
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto macrotime() const noexcept -> std::uint16_t {
        return read_u16le(byte_subspan<0, 2>(bytes)) & 0x0fffu;
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] auto marker_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 4)) != 0;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto marker_bits() const noexcept -> std::uint8_t {
        return routing_signals();
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto gap_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 5)) != 0;
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto macrotime_overflow_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 6)) != 0;
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto invalid_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 7)) != 0;
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    [[nodiscard]] auto is_multiple_macrotime_overflow() const noexcept
        -> bool {
        // Although documentation is not clear, a marker can share an event
        // record with a (single) macrotime overflow, just as a photon can.
        return macrotime_overflow_flag() && invalid_flag() && !marker_flag();
    }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    [[nodiscard]] auto multiple_macrotime_overflow_count() const noexcept
        -> std::uint32_t {
        return read_u32le(byte_subspan<0, 4>(bytes)) & 0x0fff'ffffu;
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(bh_spc_event const &lhs,
                           bh_spc_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bh_spc_event const &lhs,
                           bh_spc_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, bh_spc_event const &e)
        -> std::ostream & {
        return strm << "bh_spc(MT=" << e.macrotime()
                    << ", ROUT=" << e.routing_signals()
                    << ", ADC=" << e.adc_value()
                    << ", INVALID=" << e.invalid_flag()
                    << ", MTOV=" << e.macrotime_overflow_flag()
                    << ", GAP=" << e.gap_flag() << ", MARK=" << e.marker_flag()
                    << ", CNT=" << e.multiple_macrotime_overflow_count()
                    << ")";
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 4096-channel mode.
 */
struct bh_spc_600_event_48 {
    /**
     * \brief Bytes of the 48-bit raw device event.
     */
    std::array<std::byte, 6> bytes;

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr macrotime macrotime_overflow_period = 1 << 24;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto adc_value() const noexcept -> std::uint16_t {
        return read_u16le(byte_subspan<0, 2>(bytes)) & 0x0fffu;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto routing_signals() const noexcept -> std::uint8_t {
        return read_u8(byte_subspan<3, 1>(bytes));
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto macrotime() const noexcept -> std::uint32_t {
        unsigned const lo8 = read_u8(byte_subspan<4, 1>(bytes));
        unsigned const mid8 = read_u8(byte_subspan<5, 1>(bytes));
        unsigned const hi8 = read_u8(byte_subspan<2, 1>(bytes));
        return lo8 | (mid8 << 8) | (hi8 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] static auto marker_flag() noexcept -> bool { return false; }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] static auto marker_bits() noexcept -> std::uint8_t {
        return 0;
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto gap_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<1, 1>(bytes)) & (1u << 6)) != 0;
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto macrotime_overflow_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<1, 1>(bytes)) & (1u << 5)) != 0;
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto invalid_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<1, 1>(bytes)) & (1u << 4)) != 0;
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    [[nodiscard]] static auto is_multiple_macrotime_overflow() noexcept
        -> bool {
        return false;
    }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    [[nodiscard]] static auto multiple_macrotime_overflow_count() noexcept
        -> std::uint32_t {
        return 0;
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(bh_spc_600_event_48 const &lhs,
                           bh_spc_600_event_48 const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bh_spc_600_event_48 const &lhs,
                           bh_spc_600_event_48 const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, bh_spc_600_event_48 const &e)
        -> std::ostream & {
        bool const unused_bit =
            (read_u8(byte_subspan<1, 1>(e.bytes)) & (1u << 7)) != 0;
        return strm << "bh_spc(MT=" << e.macrotime()
                    << ", R=" << e.routing_signals()
                    << ", ADC=" << e.adc_value()
                    << ", INVALID=" << e.invalid_flag()
                    << ", MTOV=" << e.macrotime_overflow_flag()
                    << ", GAP=" << e.gap_flag() << ", bit15=" << unused_bit
                    << ")";
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 256-channel mode.
 */
struct bh_spc_600_event_32 {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    std::array<std::byte, 4> bytes;

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr macrotime macrotime_overflow_period = 1 << 17;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto adc_value() const noexcept -> std::uint16_t {
        return read_u8(byte_subspan<0, 1>(bytes));
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto routing_signals() const noexcept -> std::uint8_t {
        return (read_u8(byte_subspan<3, 1>(bytes)) & 0x0fu) >> 1;
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto macrotime() const noexcept -> std::uint32_t {
        unsigned const lo8 = read_u8(byte_subspan<1, 1>(bytes));
        unsigned const mid8 = read_u8(byte_subspan<2, 1>(bytes));
        unsigned const hi1 = read_u8(byte_subspan<3, 1>(bytes)) & 1u;
        return lo8 | (mid8 << 8) | (hi1 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] static auto marker_flag() noexcept -> bool { return false; }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] static auto marker_bits() noexcept -> std::uint8_t {
        return 0;
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto gap_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 5)) != 0;
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto macrotime_overflow_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 6)) != 0;
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto invalid_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 7)) != 0;
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    [[nodiscard]] auto is_multiple_macrotime_overflow() const noexcept
        -> bool {
        return macrotime_overflow_flag() && invalid_flag();
    }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    [[nodiscard]] auto multiple_macrotime_overflow_count() const noexcept
        -> std::uint32_t {
        return read_u32le(byte_subspan<0, 4>(bytes)) & 0x0fff'ffff;
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(bh_spc_600_event_32 const &lhs,
                           bh_spc_600_event_32 const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bh_spc_600_event_32 const &lhs,
                           bh_spc_600_event_32 const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, bh_spc_600_event_32 const &e)
        -> std::ostream & {
        bool const unused_bit =
            (read_u8(byte_subspan<3, 1>(e.bytes)) & (1u << 4)) != 0;
        return strm << "bh_spc(MT=" << e.macrotime()
                    << ", R=" << e.routing_signals()
                    << ", ADC=" << e.adc_value()
                    << ", INVALID=" << e.invalid_flag()
                    << ", MTOV=" << e.macrotime_overflow_flag()
                    << ", GAP=" << e.gap_flag() << ", bit28=" << unused_bit
                    << ", CNT=" << e.multiple_macrotime_overflow_count()
                    << ")";
    }
};

namespace internal {

// Common implementation for decode_bh_spc, decode_bh_spc_600_48,
// decode_bh_spc_600_32. E is the binary record event class.
template <typename E, typename D> class base_decode_bh_spc {
    macrotime macrotime_base = 0; // Time of last overflow
    macrotime last_macrotime = 0;

    D downstream;

  public:
    explicit base_decode_bh_spc(D &&downstream)
        : downstream(std::move(downstream)) {}

    // Rule of zero

    void handle_event(E const &event) noexcept {
        if (event.is_multiple_macrotime_overflow()) {
            macrotime_base += E::macrotime_overflow_period *
                              event.multiple_macrotime_overflow_count();

            time_reached_event e{macrotime_base};
            downstream.handle_event(e);
            return;
        }

        if (event.macrotime_overflow_flag()) {
            macrotime_base += E::macrotime_overflow_period;
        }

        macrotime macrotime = macrotime_base + event.macrotime();

        // Validate input: ensure macrotime increases monotonically (a common
        // assumption made by downstream processors)
        if (macrotime <= last_macrotime) {
            downstream.handle_end(std::make_exception_ptr(
                std::runtime_error("Non-monotonic macrotime encountered")));
            return;
        }
        last_macrotime = macrotime;

        if (event.gap_flag()) {
            data_lost_event e{macrotime};
            downstream.handle_event(e);
        }

        if (event.marker_flag()) {
            marker_event e{{macrotime}, 0};
            std::uint32_t bits = event.marker_bits();
            while (bits != 0) {
                e.channel = count_trailing_zeros_32(bits);
                downstream.handle_event(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        if (event.invalid_flag()) {
            time_reached_event e{macrotime};
            downstream.handle_event(e);
        } else {
            time_correlated_count_event e{
                {macrotime}, event.adc_value(), event.routing_signals()};
            downstream.handle_event(e);
        }
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};
} // namespace internal

/**
 * \brief Create a processor that decodes Becker & Hickl SPC (most models) FIFO
 * records.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-bh-spc processor
 */
template <typename D> auto decode_bh_spc(D &&downstream) {
    return internal::base_decode_bh_spc<bh_spc_event, D>(
        std::forward<D>(downstream));
}

/**
 * \brief Create a processor that decodes Becker & Hickl SPC-600/630
 * 4096-channel mode FIFO records.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-bh-spc-600-48 processor
 */
template <typename D> auto decode_bh_spc_600_48(D &&downstream) {
    return internal::base_decode_bh_spc<bh_spc_600_event_48, D>(
        std::forward<D>(downstream));
}

/**
 * \brief Create a processor that decodes Becker & Hickl SPC-600/630
 * 256-channel mode FIFO records.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-bh-spc-600-32 processor
 */
template <typename D> auto decode_bh_spc_600_32(D &&downstream) {
    return internal::base_decode_bh_spc<bh_spc_600_event_32, D>(
        std::forward<D>(downstream));
}

} // namespace tcspc
