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

/**
 * \brief Binary record interpretation for raw BH SPC event.
 *
 * \ingroup events-device
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
    static constexpr std::uint32_t macrotime_overflow_period = 1 << 12;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto adc_value() const noexcept -> u16np {
        return read_u16le(byte_subspan<2, 2>(bytes)) & 0x0fff_u16np;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto routing_signals() const noexcept -> u8np {
        // The documentation somewhat confusingly says that these bits are
        // "inverted", but what they mean is that the TTL inputs are active
        // low. The bits in the FIFO data are not inverted.
        return read_u8(byte_subspan<1, 1>(bytes)) >> 4;
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto macrotime() const noexcept -> u16np {
        return read_u16le(byte_subspan<0, 2>(bytes)) & 0x0fff_u16np;
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] auto marker_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 4)) != 0_u8np;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto marker_bits() const noexcept -> u8np {
        return routing_signals();
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto gap_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 5)) != 0_u8np;
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto macrotime_overflow_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 6)) != 0_u8np;
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto invalid_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 7)) != 0_u8np;
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
        -> u32np {
        return read_u32le(byte_subspan<0, 4>(bytes)) & 0x0fff'ffff_u32np;
    }

    /**
     * \brief Set this event to represent a valid photon event.
     *
     * The gap flag is cleared.
     *
     * \param macrotime the photon macrotime; 0 to 4095
     *
     * \param adc_value the photon ADC value (microtime); 0 to 4095
     *
     * \param route the routing signals (channel); 0 to 15
     *
     * \param macrotime_overflow whether to set the macrotime overflow flag
     *
     * \return \c *this
     */
    auto assign_photon(u16np macrotime, u16np adc_value, u8np route,
                       bool macrotime_overflow = false) noexcept
        -> bh_spc_event & {
        auto const flags = (macrotime_overflow ? 0b0100_u8np : 0_u8np);
        bytes[3] = std::byte(
            ((flags << 4) | (u8np(adc_value >> 8) & 0x0f_u8np)).value());
        bytes[2] = std::byte(u8np(adc_value).value());
        bytes[1] = std::byte(
            ((route << 4) | (u8np(macrotime >> 8) & 0x0f_u8np)).value());
        bytes[0] = std::byte(u8np(macrotime).value());
        return *this;
    }

    /**
     * \brief Set this event to represent an invalid photon event.
     *
     * The gap flag is cleared. This event type does not allow invalid photons
     * to carry a macrotime overflow.
     *
     * \param macrotime the photon macrotime; 0 to 4095
     *
     * \param adc_value the photon ADC value (microtime); 0 to 4095
     *
     * \return \c *this
     */
    auto assign_invalid_photon(u16np macrotime, u16np adc_value) noexcept
        -> bh_spc_event & {
        // N.B. No MTOV.
        assign_photon(macrotime, adc_value, 0_u8np);
        static constexpr auto inv_bit = std::byte(0b1000'0000);
        bytes[3] = (bytes[3] & ~inv_bit) | inv_bit;
        return *this;
    }

    /**
     * \brief Set this event to represent a marker.
     *
     * The gap flag is cleared.
     *
     * \param macrotime the marker macrotime; 0 to 4095
     *
     * \param marker_bits the marker bitmask; 1 to 15 (0 is allowed but may not
     * be handled correctly by other readers)
     *
     * \param macrotime_overflow whether to set the macrotime overflow flag
     *
     * \return \c *this
     */
    auto assign_marker(u16np macrotime, u8np marker_bits,
                       bool macrotime_overflow = false) noexcept
        -> bh_spc_event & {
        auto const flags =
            0b1001_u8np | (macrotime_overflow ? 0b0100_u8np : 0_u8np);
        static constexpr auto adc_value = 0_u16np;
        bytes[3] = std::byte(
            ((flags << 4) | (u8np(adc_value >> 8) & 0x0f_u8np)).value());
        bytes[2] = std::byte(u8np(adc_value).value());
        bytes[1] = std::byte(
            ((marker_bits << 4) | (u8np(macrotime >> 8) & 0x0f_u8np)).value());
        bytes[0] = std::byte(u8np(macrotime).value());
        return *this;
    }

    /**
     * \brief Set this event to represent a multiple macrotime overflow.
     *
     * The gap flag is cleared.
     *
     * \param count the number of macrotime overflows; 1 to 268,435,455 (0 is
     * allowed but may not be handled correctly by other readers)
     *
     * \return \c *this
     */
    auto assign_multiple_macrotime_overflow(u32np count) noexcept
        -> bh_spc_event & {
        static constexpr auto flags = 0b1100_u8np;
        bytes[3] = std::byte(
            ((flags << 4) | (u8np(count >> 24) & 0x0f_u8np)).value());
        bytes[2] = std::byte(u8np(count >> 16).value());
        bytes[1] = std::byte(u8np(count >> 8).value());
        bytes[0] = std::byte(u8np(count >> 0).value());
        return *this;
    }

    /**
     * \brief Set or clear the gap flag of this event.
     *
     * All other bits are unaffected.
     *
     * \param gap if true, set the gap bit; otherwise clear
     *
     * \return \c *this
     */
    auto gap_flag(bool gap) noexcept -> bh_spc_event & {
        static constexpr auto gap_bit = std::byte(0b0010'0000);
        bytes[3] = (bytes[3] & ~gap_bit) | (gap ? gap_bit : std::byte(0));
        return *this;
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
                    << ", ROUT=" << unsigned(e.routing_signals().value())
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
 *
 * \ingroup events-device
 */
struct bh_spc600_4096ch_event {
    /**
     * \brief Bytes of the 48-bit raw device event.
     */
    std::array<std::byte, 6> bytes;

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr std::uint32_t macrotime_overflow_period = 1 << 24;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto adc_value() const noexcept -> u16np {
        return read_u16le(byte_subspan<0, 2>(bytes)) & 0x0fff_u16np;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto routing_signals() const noexcept -> u8np {
        return read_u8(byte_subspan<3, 1>(bytes));
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto macrotime() const noexcept -> u32np {
        auto const lo8 = u32np(read_u8(byte_subspan<4, 1>(bytes)));
        auto const mid8 = u32np(read_u8(byte_subspan<5, 1>(bytes)));
        auto const hi8 = u32np(read_u8(byte_subspan<2, 1>(bytes)));
        return lo8 | (mid8 << 8) | (hi8 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] static auto marker_flag() noexcept -> bool { return false; }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] static auto marker_bits() noexcept -> u8np { return 0_u8np; }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto gap_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<1, 1>(bytes)) & (1_u8np << 6)) != 0_u8np;
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto macrotime_overflow_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<1, 1>(bytes)) & (1_u8np << 5)) != 0_u8np;
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto invalid_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<1, 1>(bytes)) & (1_u8np << 4)) != 0_u8np;
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
        -> u32np {
        return 0_u32np;
    }

    /**
     * \brief Set this event to represent a valid photon event.
     *
     * The gap flag is cleared.
     *
     * \param macrotime the photon macrotime; 0 to 16,777,215
     *
     * \param adc_value the photon ADC value (microtime); 0 to 4095
     *
     * \param route the routing signals (channel); 0 to 255
     *
     * \param macrotime_overflow whether to set the macrotime overflow flag
     *
     * \return \c *this
     */
    auto assign_photon(u32np macrotime, u16np adc_value, u8np route,
                       bool macrotime_overflow = false) noexcept
        -> bh_spc600_4096ch_event & {
        auto const flags = (macrotime_overflow ? 0b0010_u8np : 0_u8np);
        bytes[5] = std::byte(u8np(macrotime >> 8).value());
        bytes[4] = std::byte(u8np(macrotime >> 0).value());
        bytes[3] = std::byte(route.value());
        bytes[2] = std::byte(u8np(macrotime >> 16).value());
        bytes[1] = std::byte(
            ((flags << 4) | (u8np(adc_value >> 8) & 0x0f_u8np)).value());
        bytes[0] = std::byte(u8np(adc_value).value());
        return *this;
    }

    /**
     * \brief Set this event to represent an invalid photon event.
     *
     * The gap flag is cleared.
     *
     * \param macrotime the photon macrotime; 0 to 16,777,215
     *
     * \param adc_value the photon ADC value (microtime); 0 to 4095
     *
     * \param macrotime_overflow whether to set the macrotime overflow flag
     *
     * \return \c *this
     */
    auto assign_invalid_photon(u32np macrotime, u16np adc_value,
                               bool macrotime_overflow = false) noexcept
        -> bh_spc600_4096ch_event & {
        assign_photon(macrotime, adc_value, 0_u8np, macrotime_overflow);
        static constexpr auto inv_bit = std::byte(0b0001'0000);
        bytes[1] = (bytes[1] & ~inv_bit) | inv_bit;
        return *this;
    }

    /**
     * \brief Set or clear the gap flag of this event.
     *
     * All other bits are unaffected.
     *
     * \param gap if true, set the gap bit; otherwise clear
     *
     * \return \c *this
     */
    auto gap_flag(bool gap) noexcept -> bh_spc600_4096ch_event & {
        static constexpr auto gap_bit = std::byte(0b0100'0000);
        bytes[1] = (bytes[1] & ~gap_bit) | (gap ? gap_bit : std::byte(0));
        return *this;
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(bh_spc600_4096ch_event const &lhs,
                           bh_spc600_4096ch_event const &rhs) noexcept
        -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bh_spc600_4096ch_event const &lhs,
                           bh_spc600_4096ch_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, bh_spc600_4096ch_event const &e)
        -> std::ostream & {
        bool const unused_bit =
            (read_u8(byte_subspan<1, 1>(e.bytes)) & (1_u8np << 7)) != 0_u8np;
        return strm << "bh_spc600_4096ch(MT=" << e.macrotime()
                    << ", R=" << unsigned(e.routing_signals().value())
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
 *
 * \ingroup events-device
 */
struct bh_spc600_256ch_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    std::array<std::byte, 4> bytes;

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr std::uint32_t macrotime_overflow_period = 1 << 17;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto adc_value() const noexcept -> u16np {
        return u16np(read_u8(byte_subspan<0, 1>(bytes)));
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto routing_signals() const noexcept -> u8np {
        return (read_u8(byte_subspan<3, 1>(bytes)) & 0x0f_u8np) >> 1;
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto macrotime() const noexcept -> u32np {
        auto const lo8 = u32np(read_u8(byte_subspan<1, 1>(bytes)));
        auto const mid8 = u32np(read_u8(byte_subspan<2, 1>(bytes)));
        auto const hi1 = u32np(read_u8(byte_subspan<3, 1>(bytes))) & 1_u32np;
        return lo8 | (mid8 << 8) | (hi1 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] static auto marker_flag() noexcept -> bool { return false; }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] static auto marker_bits() noexcept -> u8np { return 0_u8np; }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto gap_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 5)) != 0_u8np;
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto macrotime_overflow_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 6)) != 0_u8np;
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto invalid_flag() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1_u8np << 7)) != 0_u8np;
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
        -> u32np {
        return read_u32le(byte_subspan<0, 4>(bytes)) & 0x0fff'ffff_u32np;
    }

    /**
     * \brief Set this event to represent a valid photon event.
     *
     * The gap flag is cleared.
     *
     * \param macrotime the photon macrotime; 0 to 131,071
     *
     * \param adc_value the photon ADC value (microtime); 0 to 255
     *
     * \param route the routing signals (channel); 0 to 7
     *
     * \param macrotime_overflow whether to set the macrotime overflow flag
     *
     * \return \c *this
     */
    auto assign_photon(u32np macrotime, u8np adc_value, u8np route,
                       bool macrotime_overflow = false) noexcept
        -> bh_spc600_256ch_event & {
        auto const flags = (macrotime_overflow ? 0b0100_u8np : 0_u8np);
        bytes[3] = std::byte(((flags << 4) | ((route << 1) & 0b1110_u8np) |
                              (u8np(macrotime >> 16) & 0x01_u8np))
                                 .value());
        bytes[2] = std::byte(u8np(macrotime >> 8).value());
        bytes[1] = std::byte(u8np(macrotime).value());
        bytes[0] = std::byte(adc_value.value());
        return *this;
    }

    /**
     * \brief Set this event to represent an invalid photon event.
     *
     * The gap flag is cleared. This event type does not allow invalid photons
     * to carry a macrotime overflow.
     *
     * \param macrotime the photon macrotime; 0 to 131,071
     *
     * \param adc_value the photon ADC value (microtime); 0 to 255
     *
     * \return \c *this
     */
    auto assign_invalid_photon(u32np macrotime, u8np adc_value) noexcept
        -> bh_spc600_256ch_event & {
        // N.B. No MTOV.
        assign_photon(macrotime, adc_value, 0_u8np);
        static constexpr auto inv_bit = std::byte(0b1000'0000);
        bytes[3] = (bytes[3] & ~inv_bit) | inv_bit;
        return *this;
    }

    /**
     * \brief Set this event to represent a multiple macrotime overflow.
     *
     * The gap flag is cleared.
     *
     * \param count the number of macrotime overflows; 1 to 268,435,455 (0 is
     * allowed but may not be handled correctly by other readers)
     *
     * \return \c *this
     */
    auto assign_multiple_macrotime_overflow(u32np count) noexcept
        -> bh_spc600_256ch_event & {
        static constexpr auto flags = 0b1100_u8np;
        bytes[3] = std::byte(
            ((flags << 4) | (u8np(count >> 24) & 0x0f_u8np)).value());
        bytes[2] = std::byte(u8np(count >> 16).value());
        bytes[1] = std::byte(u8np(count >> 8).value());
        bytes[0] = std::byte(u8np(count >> 0).value());
        return *this;
    }

    /**
     * \brief Set or clear the gap flag of this event.
     *
     * All other bits are unaffected.
     *
     * \param gap if true, set the gap bit; otherwise clear
     *
     * \return \c *this
     */
    auto gap_flag(bool gap) noexcept -> bh_spc600_256ch_event & {
        static constexpr auto gap_bit = std::byte(0b0010'0000);
        bytes[3] = (bytes[3] & ~gap_bit) | (gap ? gap_bit : std::byte(0));
        return *this;
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(bh_spc600_256ch_event const &lhs,
                           bh_spc600_256ch_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(bh_spc600_256ch_event const &lhs,
                           bh_spc600_256ch_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, bh_spc600_256ch_event const &e)
        -> std::ostream & {
        bool const unused_bit =
            (read_u8(byte_subspan<3, 1>(e.bytes)) & (1_u8np << 4)) != 0_u8np;
        return strm << "bh_spc600_256ch(MT=" << e.macrotime()
                    << ", R=" << unsigned(e.routing_signals().value())
                    << ", ADC=" << e.adc_value()
                    << ", INVALID=" << e.invalid_flag()
                    << ", MTOV=" << e.macrotime_overflow_flag()
                    << ", GAP=" << e.gap_flag() << ", bit28=" << unused_bit
                    << ", CNT=" << e.multiple_macrotime_overflow_count()
                    << ")";
    }
};

namespace internal {

// Common implementation for decode_bh_spc*.
// BHEvent is the binary record event class.
template <typename DataTraits, typename BHSPCEvent, typename Downstream>
class decode_bh_spc {
    using abstime_type = typename DataTraits::abstime_type;

    abstime_type abstime_base = 0; // Time of last overflow

    Downstream downstream;

    LIBTCSPC_NOINLINE void issue_warning(char const *message) noexcept {
        downstream.handle_event(warning_event{message});
    }

  public:
    explicit decode_bh_spc(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    // Rule of zero

    void handle_event(BHSPCEvent const &event) noexcept {
        if (event.is_multiple_macrotime_overflow()) {
            abstime_base +=
                abstime_type(BHSPCEvent::macrotime_overflow_period) *
                event.multiple_macrotime_overflow_count().value();
            if (event.gap_flag())
                downstream.handle_event(
                    data_lost_event<DataTraits>{abstime_base});
            return downstream.handle_event(
                time_reached_event<DataTraits>{abstime_base});
        }

        if (event.macrotime_overflow_flag())
            abstime_base += BHSPCEvent::macrotime_overflow_period;
        abstime_type const abstime = abstime_base + event.macrotime().value();

        if (event.gap_flag())
            downstream.handle_event(data_lost_event<DataTraits>{abstime});

        if (not event.marker_flag()) {
            if (not event.invalid_flag()) { // Valid photon
                downstream.handle_event(
                    time_correlated_detection_event<DataTraits>{
                        {{abstime}, event.routing_signals().value()},
                        event.adc_value().value()});
            } else { // Invalid photon
                downstream.handle_event(
                    time_reached_event<DataTraits>{abstime});
            }
        } else {
            if (event.invalid_flag()) { // Marker
                auto bits = u32np(event.marker_bits());
                while (bits != 0_u32np) {
                    downstream.handle_event(marker_event<DataTraits>{
                        {{abstime},
                         static_cast<typename DataTraits::channel_type>(
                             count_trailing_zeros_32(bits))}});
                    bits = bits & (bits - 1_u32np); // Clear the handled bit
                }
            } else {
                // Although not clearly documented, the combination of
                // INV=0, MARK=1 is not currently used.
                issue_warning(
                    "unexpected BH SPC event flags: marker bit set but invalid bit cleared");
            }
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
 * \ingroup processors-decode
 *
 * \tparam DataTraits traits type specifying \c abstime_type, \c channel_type,
 * and \c difftime_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return decode-bh-spc processor
 */
template <typename DataTraits, typename Downstream>
auto decode_bh_spc(Downstream &&downstream) {
    return internal::decode_bh_spc<DataTraits, bh_spc_event, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes Becker & Hickl SPC-600/630
 * 4096-channel mode FIFO records.
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
 * \return decode-bh-spc-600-4096ch processor
 */
template <typename DataTraits, typename Downstream>
auto decode_bh_spc600_4096ch(Downstream &&downstream) {
    return internal::decode_bh_spc<DataTraits, bh_spc600_4096ch_event,
                                   Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes Becker & Hickl SPC-600/630
 * 256-channel mode FIFO records.
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
 * \return decode-bh-spc-600-256ch processor
 */
template <typename DataTraits, typename Downstream>
auto decode_bh_spc600_256ch(Downstream &&downstream) {
    return internal::decode_bh_spc<DataTraits, bh_spc600_256ch_event,
                                   Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
