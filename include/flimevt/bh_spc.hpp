/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

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
struct bh_spc_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    unsigned char bytes[4];

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr macrotime macrotime_overflow_period = 1 << 12;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto get_adc_value() const noexcept -> std::uint16_t {
        return unsigned(internal::read_u16le(&bytes[2])) & 0x0fffu;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto get_routing_signals() const noexcept -> std::uint8_t {
        // The documentation somewhat confusingly says that these bits are
        // "inverted", but what they mean is that the TTL inputs are active
        // low. The bits in the FIFO data are not inverted.
        return static_cast<std::uint8_t>(unsigned(bytes[1]) >> 4);
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto get_macrotime() const noexcept -> std::uint16_t {
        return unsigned(internal::read_u16le(&bytes[0])) & 0x0fffu;
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] auto get_marker_flag() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 4);
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto get_marker_bits() const noexcept -> std::uint8_t {
        return get_routing_signals();
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto get_gap_flag() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 5);
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto get_macrotime_overflow_flag() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 6);
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto get_invalid_flag() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 7);
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    [[nodiscard]] auto is_multiple_macrotime_overflow() const noexcept
        -> bool {
        // Although documentation is not clear, a marker can share an event
        // record with a (single) macrotime overflow, just as a photon can.
        return get_macrotime_overflow_flag() && get_invalid_flag() &&
               !get_marker_flag();
    }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    [[nodiscard]] auto get_multiple_macrotime_overflow_count() const noexcept
        -> std::uint32_t {
        return internal::read_u32le(&bytes[0]) & 0x0fffffffu;
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
    unsigned char bytes[6];

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr macrotime macrotime_overflow_period = 1 << 24;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto get_adc_value() const noexcept -> std::uint16_t {
        return unsigned(internal::read_u16le(&bytes[0])) & 0x0fffu;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto get_routing_signals() const noexcept -> std::uint8_t {
        return bytes[3];
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto get_macrotime() const noexcept -> std::uint32_t {
        auto lo8 = unsigned(bytes[4]);
        auto mid8 = unsigned(bytes[5]);
        auto hi8 = unsigned(bytes[2]);
        return lo8 | (mid8 << 8) | (hi8 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] auto get_marker_flag() const noexcept -> bool {
        return false;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto get_marker_bits() const noexcept -> std::uint8_t {
        return 0;
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto get_gap_flag() const noexcept -> bool {
        return unsigned(bytes[1]) & (1u << 6);
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto get_macrotime_overflow_flag() const noexcept -> bool {
        return unsigned(bytes[1]) & (1u << 5);
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto get_invalid_flag() const noexcept -> bool {
        return unsigned(bytes[1]) & (1u << 4);
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    [[nodiscard]] auto is_multiple_macrotime_overflow() const noexcept
        -> bool {
        return false;
    }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    [[nodiscard]] auto get_multiple_macrotime_overflow_count() const noexcept
        -> std::uint32_t {
        return 0;
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
    unsigned char bytes[4];

    /**
     * \brief The macrotime overflow period of this event type.
     */
    static constexpr macrotime macrotime_overflow_period = 1 << 17;

    /**
     * \brief Read the ADC value (i.e., difference time) if this event
     * represents a photon.
     */
    [[nodiscard]] auto get_adc_value() const noexcept -> std::uint16_t {
        return bytes[0];
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    [[nodiscard]] auto get_routing_signals() const noexcept -> std::uint8_t {
        return (bytes[3] & 0x0f) >> 1;
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    [[nodiscard]] auto get_macrotime() const noexcept -> std::uint32_t {
        auto lo8 = unsigned(bytes[1]);
        auto mid8 = unsigned(bytes[2]);
        auto hi1 = unsigned(bytes[3]) & 1u;
        return lo8 | (mid8 << 8) | (hi1 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    [[nodiscard]] auto get_marker_flag() const noexcept -> bool {
        return false;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto get_marker_bits() const noexcept -> std::uint8_t {
        return 0;
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    [[nodiscard]] auto get_gap_flag() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 5);
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    [[nodiscard]] auto get_macrotime_overflow_flag() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 6);
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    [[nodiscard]] auto get_invalid_flag() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 7);
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    [[nodiscard]] auto is_multiple_macrotime_overflow() const noexcept
        -> bool {
        return false;
    }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    [[nodiscard]] auto get_multiple_macrotime_overflow_count() const noexcept
        -> std::uint32_t {
        return 0;
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
                              event.get_multiple_macrotime_overflow_count();

            time_reached_event e{macrotime_base};
            downstream.handle_event(e);
            return;
        }

        if (event.get_macrotime_overflow_flag()) {
            macrotime_base += E::macrotime_overflow_period;
        }

        macrotime macrotime = macrotime_base + event.get_macrotime();

        // Validate input: ensure macrotime increases monotonically (a common
        // assumption made by downstream processors)
        if (macrotime <= last_macrotime) {
            downstream.handle_end(std::make_exception_ptr(
                std::runtime_error("Non-monotonic macrotime encountered")));
            return;
        }
        last_macrotime = macrotime;

        if (event.get_gap_flag()) {
            data_lost_event e{macrotime};
            downstream.handle_event(e);
        }

        if (event.get_marker_flag()) {
            marker_event e{{macrotime}, 0};
            std::uint32_t bits = event.get_marker_bits();
            while (bits) {
                e.channel = count_trailing_zeros_32(bits);
                downstream.handle_event(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        if (event.get_invalid_flag()) {
            time_reached_event e{macrotime};
            downstream.handle_event(e);
        } else {
            time_correlated_count_event e{{macrotime},
                                          event.get_adc_value(),
                                          event.get_routing_signals()};
            downstream.handle_event(e);
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
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

} // namespace flimevt
