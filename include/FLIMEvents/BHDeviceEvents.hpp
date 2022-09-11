/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Common.hpp"
#include "EventSet.hpp"
#include "ReadBytes.hpp"
#include "TimeTaggedEvents.hpp"

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
    std::uint16_t get_adc_value() const noexcept {
        return unsigned(internal::read_u16le(&bytes[2])) & 0x0fffU;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    std::uint8_t get_routing_signals() const noexcept {
        // The documentation somewhat confusingly says that these bits are
        // "inverted", but what they mean is that the TTL inputs are active
        // low. The bits in the FIFO data are not inverted.
        return unsigned(bytes[1]) >> 4;
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    std::uint16_t get_macrotime() const noexcept {
        return unsigned(internal::read_u16le(&bytes[0])) & 0x0fffU;
    }

    /**
     * \brief Read the 'marker' flag.
     */
    bool get_marker_flag() const noexcept {
        return unsigned(bytes[3]) & (1U << 4);
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    std::uint8_t get_marker_bits() const noexcept {
        return get_routing_signals();
    }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    bool get_gap_flag() const noexcept {
        return unsigned(bytes[3]) & (1U << 5);
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    bool get_macrotime_overflow_flag() const noexcept {
        return unsigned(bytes[3]) & (1U << 6);
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    bool get_invalid_flag() const noexcept {
        return unsigned(bytes[3]) & (1U << 7);
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    bool is_multiple_macrotime_overflow() const noexcept {
        // Although documentation is not clear, a marker can share an event
        // record with a (single) macrotime overflow, just as a photon can.
        return get_macrotime_overflow_flag() && get_invalid_flag() &&
               !get_marker_flag();
    }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    std::uint32_t get_multiple_macrotime_overflow_count() const noexcept {
        return internal::read_u32le(&bytes[0]) & 0x0fffffffU;
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
    std::uint16_t get_adc_value() const noexcept {
        return unsigned(internal::read_u16le(&bytes[0])) & 0x0fffU;
    }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    std::uint8_t get_routing_signals() const noexcept { return bytes[3]; }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    std::uint32_t get_macrotime() const noexcept {
        auto lo8 = unsigned(bytes[4]);
        auto mid8 = unsigned(bytes[5]);
        auto hi8 = unsigned(bytes[2]);
        return lo8 | (mid8 << 8) | (hi8 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    bool get_marker_flag() const noexcept { return false; }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    std::uint8_t get_marker_bits() const noexcept { return 0; }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    bool get_gap_flag() const noexcept {
        return unsigned(bytes[1]) & (1U << 6);
    }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    bool get_macrotime_overflow_flag() const noexcept {
        return unsigned(bytes[1]) & (1U << 5);
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    bool get_invalid_flag() const noexcept {
        return unsigned(bytes[1]) & (1U << 4);
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    bool is_multiple_macrotime_overflow() const noexcept { return false; }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    std::uint32_t get_multiple_macrotime_overflow_count() const noexcept {
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
    std::uint16_t get_adc_value() const noexcept { return bytes[0]; }

    /**
     * \brief Read the routing signals (usually the detector channel) if this
     * event represents a photon.
     */
    std::uint8_t get_routing_signals() const noexcept {
        return (bytes[3] & 0x0f) >> 1;
    }

    /**
     * \brief Read the macrotime counter value (no rollover correction).
     */
    std::uint32_t get_macrotime() const noexcept {
        auto lo8 = unsigned(bytes[1]);
        auto mid8 = unsigned(bytes[2]);
        auto hi1 = unsigned(bytes[3]) & 1U;
        return lo8 | (mid8 << 8) | (hi1 << 16);
    }

    /**
     * \brief Read the 'marker' flag.
     */
    bool get_marker_flag() const noexcept { return false; }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    std::uint8_t get_marker_bits() const noexcept { return 0; }

    /**
     * \brief Read the 'gap' (data lost) flag.
     */
    bool GetGapflag() const noexcept { return unsigned(bytes[3]) & (1U << 5); }

    /**
     * \brief Read the 'macrotime overflow' flag.
     */
    bool get_macrotime_overflow_flag() const noexcept {
        return unsigned(bytes[3]) & (1U << 6);
    }

    /**
     * \brief Read the 'invalid' flag.
     */
    bool get_invalid_flag() const noexcept {
        return unsigned(bytes[3]) & (1U << 7);
    }

    /**
     * \brief Determine if this event represents multiple macrotime overflows.
     */
    bool is_multiple_macrotime_overflow() const noexcept { return false; }

    /**
     * \brief Read the macrotime overflow count if this event represents
     * multiple macrotime overflows.
     */
    std::uint32_t get_multiple_macrotime_overflow_count() const noexcept {
        return 0;
    }
};

namespace internal {

// Common implementation for decode_bh_spc, decode_bh_spc_600_48,
// decode_bh_spc_600_32. E is the binary record event class.
template <typename E, typename D> class base_decode_bh_spc {
    macrotime macrotimeBase; // Time of last overflow
    macrotime lastMacrotime;

    D downstream;

  public:
    explicit base_decode_bh_spc(D &&downstream)
        : macrotimeBase(0), lastMacrotime(0),
          downstream(std::move(downstream)) {}

    // Rule of zero

    void handle_event(E const &event) noexcept {
        if (event.is_multiple_macrotime_overflow()) {
            macrotimeBase += E::macrotime_overflow_period *
                             event.get_multiple_macrotime_overflow_count();

            time_reached_event e;
            e.macrotime = macrotimeBase;
            downstream.handle_event(e);
            return;
        }

        if (event.get_macrotime_overflow_flag()) {
            macrotimeBase += E::macrotime_overflow_period;
        }

        macrotime macrotime = macrotimeBase + event.get_macrotime();

        // Validate input: ensure macrotime increases monotonically (a common
        // assumption made by downstream processors)
        if (macrotime <= lastMacrotime) {
            downstream.handle_end(std::make_exception_ptr(
                std::runtime_error("Non-monotonic macrotime encountered")));
            return;
        }
        lastMacrotime = macrotime;

        if (event.get_gap_flag()) {
            data_lost_event e;
            e.macrotime = macrotime;
            downstream.handle_event(e);
        }

        if (event.get_marker_flag()) {
            marker_event e;
            e.macrotime = macrotime;
            std::uint32_t bits = event.get_marker_bits();
            while (bits) {
                e.channel = internal::count_trailing_zeros_32(bits);
                downstream.handle_event(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        if (event.get_invalid_flag()) {
            time_reached_event e;
            e.macrotime = macrotime;
            downstream.handle_event(e);
        } else {
            time_correlated_count_event e;
            e.macrotime = macrotime;
            e.difftime = event.get_adc_value();
            e.channel = event.get_routing_signals();
            downstream.handle_event(e);
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};
} // namespace internal

/**
 * \brief Processor that decodes raw BH SPC (most models) events.
 *
 * \see decode_bh_spc()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class decode_bh_spc : public internal::base_decode_bh_spc<bh_spc_event, D> {
  public:
    using internal::base_decode_bh_spc<bh_spc_event, D>::base_decode_bh_spc;
};

/**
 * \brief Deduction guide for constructing a decode_bh_spc processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D> decode_bh_spc(D &&downstream) -> decode_bh_spc<D>;

/**
 * \brief Processor that decodes raw BH SPC-600/630 events in 4096-channel
 * mode.
 *
 * \see decode_bh_spc_600_48()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class decode_bh_spc_600_48
    : public internal::base_decode_bh_spc<bh_spc_600_event_48, D> {
  public:
    using internal::base_decode_bh_spc<bh_spc_600_event_48,
                                       D>::base_decode_bh_spc;
};

/**
 * \brief Deduction guide for constructing a decode_bh_spc_600_48 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
decode_bh_spc_600_48(D &&downstream) -> decode_bh_spc_600_48<D>;

/**
 * \brief Processor that decodes raw BH SPC-600/630 events in 256-channel mode.
 *
 * \see decode_bh_spc_600_32()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class decode_bh_spc_600_32
    : public internal::base_decode_bh_spc<bh_spc_600_event_32, D> {
  public:
    using internal::base_decode_bh_spc<bh_spc_600_event_32,
                                       D>::base_decode_bh_spc;
};

/**
 * \brief Deduction guide for constructing a decode_bh_spc_600_32 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
decode_bh_spc_600_32(D &&downstream) -> decode_bh_spc_600_32<D>;

/**
 * \brief Event set for raw BH SPC data stream.
 */
using bh_spc_events = event_set<bh_spc_event>;

/**
 * \brief Event set for raw BH SPC-600/630 data stream in 4096-channel mode.
 */
using bh_spc_600_events_48 = event_set<bh_spc_600_event_48>;

/**
 * \brief Event set for raw BH SPC-600/630 data stream in 256-channel mode.
 */
using bh_spc_600_events_32 = event_set<bh_spc_600_event_32>;

} // namespace flimevt
