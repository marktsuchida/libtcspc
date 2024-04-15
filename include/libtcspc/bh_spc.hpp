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
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace tcspc {

// Raw photon event data formats are documented in The bh TCSPC Handbook (see
// section on FIFO Files in the chapter on Data file structure).

/**
 * \brief Binary record interpretation for raw BH SPC event.
 *
 * \ingroup events-bh
 *
 * This interprets the FIFO format used by most BH SPC models, except for
 * SPC-600, SPC-630, and TDC models.
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
     * \brief Make an event representing a valid photon event.
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
     * \return event
     */
    static auto make_photon(u16np macrotime, u16np adc_value, u8np route,
                            bool macrotime_overflow = false) -> bh_spc_event {
        return make_from_fields(false, macrotime_overflow, false, false,
                                adc_value, route, macrotime);
    }

    /**
     * \brief Make an event representing an invalid photon event.
     *
     * The gap flag is cleared. This event type does not allow invalid photons
     * to carry a macrotime overflow.
     *
     * \param macrotime the photon macrotime; 0 to 4095
     *
     * \param adc_value the photon ADC value (microtime); 0 to 4095
     *
     * \return event
     */
    static auto make_invalid_photon(u16np macrotime, u16np adc_value)
        -> bh_spc_event {
        // N.B. No MTOV.
        return make_from_fields(true, false, false, false, adc_value, 0_u8np,
                                macrotime);
    }

    /**
     * \brief Make an event representing a marker.
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
     * \return event
     */
    static auto make_marker(u16np macrotime, u8np marker_bits,
                            bool macrotime_overflow = false) -> bh_spc_event {
        return make_from_fields(true, macrotime_overflow, false, true, 0_u16np,
                                marker_bits, macrotime);
    }

    /**
     * \brief Make an event representing a marker, with marker 0 intensity
     * count as generated by SPC-180.
     *
     * The gap flag is cleared.
     *
     * \param macrotime the marker macrotime; 0 to 4095
     *
     * \param marker_bits the marker bitmask; 1 to 15; bit 0 must be set
     *
     * \param count the intensity counter value (photons since previous marker
     * 0); 0 to 4095
     *
     * \param macrotime_overflow whether to set the macrotime overflow flag
     *
     * \return event
     */
    static auto make_spc180_marker0_with_intensity_count(
        u16np macrotime, u8np marker_bits, u16np count,
        bool macrotime_overflow = false) -> bh_spc_event {
        if ((marker_bits & 0x01_u8np) == 0_u8np)
            throw std::invalid_argument(
                "bit for marker 0 must be set in intensity counter event");
        return make_from_fields(true, macrotime_overflow, false, true, count,
                                marker_bits, macrotime);
    }

    /**
     * \brief Make an event representing a multiple macrotime overflow.
     *
     * The gap flag is cleared.
     *
     * \param count the number of macrotime overflows; 1 to 268,435,455 (0 is
     * allowed but may not be handled correctly by other readers)
     *
     * \return event
     */
    static auto make_multiple_macrotime_overflow(u32np count) -> bh_spc_event {
        static constexpr auto flags = 0b1100'0000_u8np;
        return bh_spc_event{{
            std::byte(u8np(count >> 0).value()),
            std::byte(u8np(count >> 8).value()),
            std::byte(u8np(count >> 16).value()),
            std::byte((flags | (u8np(count >> 24) & 0x0f_u8np)).value()),
        }};
    }

    /**
     * \brief Set or clear the gap flag of this event.
     *
     * All other bits are unaffected.
     *
     * \param gap if true, set the gap bit; otherwise clear
     *
     * \return event
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

  private:
    static auto make_from_fields(bool invalid, bool mtov, bool gap, bool mark,
                                 u16np adc, u8np rout, u16np mt)
        -> bh_spc_event {
        auto const flags = (u8np(u8(invalid)) << 7) | (u8np(u8(mtov)) << 6) |
                           (u8np(u8(gap)) << 5) | (u8np(u8(mark)) << 4);
        return bh_spc_event{{
            std::byte(u8np(mt).value()),
            std::byte(((rout << 4) | (u8np(mt >> 8) & 0x0f_u8np)).value()),
            std::byte(u8np(adc).value()),
            std::byte((flags | (u8np(adc >> 8) & 0x0f_u8np)).value()),
        }};
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 4096-channel mode.
 *
 * \ingroup events-bh
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
     * \brief Make an event representing a valid photon event.
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
     * \return event
     */
    static auto make_photon(u32np macrotime, u16np adc_value, u8np route,
                            bool macrotime_overflow = false)
        -> bh_spc600_4096ch_event {
        return make_from_fields(macrotime, route, false, macrotime_overflow,
                                false, adc_value);
    }

    /**
     * \brief Make an event representing an invalid photon event.
     *
     * The gap flag is cleared.
     *
     * \param macrotime the photon macrotime; 0 to 16,777,215
     *
     * \param adc_value the photon ADC value (microtime); 0 to 4095
     *
     * \param macrotime_overflow whether to set the macrotime overflow flag
     *
     * \return event
     */
    static auto make_invalid_photon(u32np macrotime, u16np adc_value,
                                    bool macrotime_overflow = false)
        -> bh_spc600_4096ch_event {
        return make_from_fields(macrotime, 0_u8np, false, macrotime_overflow,
                                true, adc_value);
    }

    /**
     * \brief Set or clear the gap flag of this event.
     *
     * All other bits are unaffected.
     *
     * \param gap if true, set the gap bit; otherwise clear
     *
     * \return event
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

  private:
    static auto make_from_fields(u32np mt, u8np r, bool gap, bool mtov,
                                 bool invalid, u16np adc)
        -> bh_spc600_4096ch_event {
        auto const flags = (u8np(u8(gap)) << 6) | (u8np(u8(mtov)) << 5) |
                           (u8np(u8(invalid)) << 4);
        return bh_spc600_4096ch_event{{
            std::byte(u8np(adc).value()),
            std::byte((flags | (u8np(adc >> 8) & 0x0f_u8np)).value()),
            std::byte(u8np(mt >> 16).value()),
            std::byte(r.value()),
            std::byte(u8np(mt >> 0).value()),
            std::byte(u8np(mt >> 8).value()),
        }};
    }
};

/**
 * \brief Binary record interpretation for raw events from SPC-600/630 in
 * 256-channel mode.
 *
 * \ingroup events-bh
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
     * \brief Make an event representing a valid photon event.
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
     * \return event
     */
    static auto make_photon(u32np macrotime, u8np adc_value, u8np route,
                            bool macrotime_overflow = false)
        -> bh_spc600_256ch_event {
        return make_from_fields(false, macrotime_overflow, false, route,
                                macrotime, adc_value);
    }

    /**
     * \brief Make an event representing an invalid photon event.
     *
     * The gap flag is cleared. This event type does not allow invalid photons
     * to carry a macrotime overflow.
     *
     * \param macrotime the photon macrotime; 0 to 131,071
     *
     * \param adc_value the photon ADC value (microtime); 0 to 255
     *
     * \return event
     */
    static auto make_invalid_photon(u32np macrotime, u8np adc_value)
        -> bh_spc600_256ch_event {
        // N.B. No MTOV.
        return make_from_fields(true, false, false, 0_u8np, macrotime,
                                adc_value);
    }

    /**
     * \brief Make an event representing a multiple macrotime overflow.
     *
     * The gap flag is cleared.
     *
     * \param count the number of macrotime overflows; 1 to 268,435,455 (0 is
     * allowed but may not be handled correctly by other readers)
     *
     * \return event
     */
    static auto make_multiple_macrotime_overflow(u32np count)
        -> bh_spc600_256ch_event {
        static constexpr auto flags = 0b1100'0000_u8np;
        return bh_spc600_256ch_event{{
            std::byte(u8np(count >> 0).value()),
            std::byte(u8np(count >> 8).value()),
            std::byte(u8np(count >> 16).value()),
            std::byte((flags | (u8np(count >> 24) & 0x0f_u8np)).value()),
        }};
    }

    /**
     * \brief Set or clear the gap flag of this event.
     *
     * All other bits are unaffected.
     *
     * \param gap if true, set the gap bit; otherwise clear
     *
     * \return event
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

  private:
    static auto make_from_fields(bool invalid, bool mtov, bool gap, u8np r,
                                 u32np mt, u8np adc) -> bh_spc600_256ch_event {
        auto const flags = (u8np(u8(invalid)) << 7) | (u8np(u8(mtov)) << 6) |
                           (u8np(u8(gap)) << 5);
        return bh_spc600_256ch_event{{
            std::byte(adc.value()),
            std::byte(u8np(mt).value()),
            std::byte(u8np(mt >> 8).value()),
            std::byte((flags | ((r << 1) & 0b1110_u8np) |
                       (u8np(mt >> 16) & 0x01_u8np))
                          .value()),
        }};
    }
};

namespace internal {

// Common implementation for decode_bh_spc*.
// BHEvent is the binary record event class.
template <typename DataTraits, typename BHSPCEvent, bool HasIntensityCounter,
          typename Downstream>
class decode_bh_spc {
    using abstime_type = typename DataTraits::abstime_type;

    abstime_type abstime_base = 0; // Time of last overflow

    Downstream downstream;

    LIBTCSPC_NOINLINE void issue_warning(char const *message) {
        downstream.handle(warning_event{message});
    }

  public:
    explicit decode_bh_spc(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "decode_bh_spc");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(BHSPCEvent const &event) {
        if (event.is_multiple_macrotime_overflow()) {
            abstime_base +=
                abstime_type(BHSPCEvent::macrotime_overflow_period) *
                event.multiple_macrotime_overflow_count().value();
            if (event.gap_flag())
                downstream.handle(data_lost_event<DataTraits>{abstime_base});
            return downstream.handle(
                time_reached_event<DataTraits>{abstime_base});
        }

        if (event.macrotime_overflow_flag())
            abstime_base += BHSPCEvent::macrotime_overflow_period;
        abstime_type const abstime = abstime_base + event.macrotime().value();

        if (event.gap_flag())
            downstream.handle(data_lost_event<DataTraits>{abstime});

        if (not event.marker_flag()) {
            if (not event.invalid_flag()) { // Valid photon
                downstream.handle(time_correlated_detection_event<DataTraits>{
                    abstime, event.routing_signals().value(),
                    event.adc_value().value()});
            } else { // Invalid photon
                downstream.handle(time_reached_event<DataTraits>{abstime});
            }
        } else {
            if (event.invalid_flag()) { // Marker
                auto const bits = u32np(event.marker_bits());
                if constexpr (HasIntensityCounter) {    // SPC-180
                    if ((bits & 0x01_u32np) != 0_u32np) // Marker 0
                        downstream.handle(nontagged_counts_event<DataTraits>{
                            abstime, -1, event.adc_value().value()});
                }
                for_each_set_bit(bits, [&](int b) {
                    downstream.handle(marker_event<DataTraits>{
                        abstime,
                        static_cast<typename DataTraits::channel_type>(b)});
                });
            } else {
                // Although not clearly documented, the combination of
                // INV=0, MARK=1 is not currently used.
                issue_warning(
                    "unexpected BH SPC event flags: marker bit set but invalid bit cleared");
            }
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that decodes FIFO records from most Becker & Hickl
 * SPC models.
 *
 * \ingroup processors-bh
 *
 * Decoder for SPC-130, 830, 140, 930, 150, 130EM, 150N (NX, NXX), 130EMN, 160
 * (X, PCIE), 180N (NX, NXX), and 130IN (INX, INXX).
 *
 * This decoder does not read the fast intensity counter values produced by
 * SPC-160 and SPC-180N (see
 * `tcspc::decode_bh_spc_with_fast_intensity_counter()`), but can be used for
 * these models if the counter value is not of interest.
 *
 * \tparam DataTraits traits type specifying `abstime_type`, `channel_type`,
 * and `difftime_type` for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bh_spc_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTraits>`,
 *   `tcspc::time_correlated_detection_event<DataTraits>`,
 *   `tcspc::marker_event<DataTraits>`, `tcspc::data_lost_event<DataTraits>`
 * - Flush: pass through with no action
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto decode_bh_spc(Downstream &&downstream) {
    return internal::decode_bh_spc<DataTraits, bh_spc_event, false,
                                   Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes FIFO records from Becker & Hickl
 * SPC-160 and SPC-180N with fast intensity counter
 *
 * \ingroup processors-bh
 *
 * Decoder for SPC-160 and SPC-180N. Generates events for the fast intensity
 * counter on marker 0. Otherwise the same as `tcspc::decode_bh_spc()`.
 *
 * \tparam DataTraits traits type specifying `abstime_type`, `channel_type`,
 * and `difftime_type` for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bh_spc_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTraits>`,
 *   `tcspc::time_correlated_detection_event<DataTraits>`,
 *   `tcspc::nontagged_counts_event<DataTraits>`
 *   `tcspc::marker_event<DataTraits>`, `tcspc::data_lost_event<DataTraits>`
 * - Flush: pass through with no action
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto decode_bh_spc_with_fast_intensity_counter(Downstream &&downstream) {
    return internal::decode_bh_spc<DataTraits, bh_spc_event, true, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes 48-bit FIFO records from Becker &
 * Hickl SPC-600/630 in 4096-channel mode.
 *
 * \ingroup processors-bh
 *
 * \tparam DataTraits traits type specifying `abstime_type`, `channel_type`,
 * and `difftime_type` for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bh_spc600_4096ch_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTraits>`,
 *   `tcspc::time_correlated_detection_event<DataTraits>`,
 *   `tcspc::data_lost_event<DataTraits>`
 * - Flush: pass through with no action
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto decode_bh_spc600_4096ch(Downstream &&downstream) {
    return internal::decode_bh_spc<DataTraits, bh_spc600_4096ch_event, false,
                                   Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that decodes 32-bit FIFO records from Becker &
 * Hickl SPC-600/630 in 256-channel mode.
 *
 * \ingroup processors-bh
 *
 * \tparam DataTraits traits type specifying `abstime_type`, `channel_type`,
 * and `difftime_type` for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::bh_spc600_256ch_event`: decode and emit one or more of
 *   `tcspc::time_reached_event<DataTraits>`,
 *   `tcspc::time_correlated_detection_event<DataTraits>`,
 *   `tcspc::data_lost_event<DataTraits>`
 * - Flush: pass through with no action
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto decode_bh_spc600_256ch(Downstream &&downstream) {
    return internal::decode_bh_spc<DataTraits, bh_spc600_256ch_event, false,
                                   Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
