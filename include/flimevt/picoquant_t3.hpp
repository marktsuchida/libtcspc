/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

namespace flimevt {

// PicoQuant raw photon event ("TTTR") formats are documented in the html files
// contained in this repository:
// https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos

// Vendor documentation does not specify, but the 32-bit records are to be
// viewed as little-endian integers when interpreting the documented bit
// locations.

// Note that code here is written to run on little- or big-endian machines; see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html

// The two T3 formats (pq_pico_t3_event and pq_hydra_t3_event) use matching
// member names for static polymorphism. This allows base_decode_pq_t3<E> to
// handle 3 different formats with the same code.

/**
 * \brief Binary record interpretation for PicoHarp T3 Format.
 *
 * RecType 0x00010303.
 */
struct pq_pico_t3_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    unsigned char bytes[4];

    /**
     * \brief The nsync overflow period of this event type.
     */
    static constexpr macrotime nsync_overflow_period = 65536;

    /**
     * \brief Read the channel if this event represents a photon.
     */
    std::uint8_t get_channel() const noexcept {
        return unsigned(bytes[3]) >> 4;
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    std::uint16_t get_dtime() const noexcept {
        return unsigned(internal::read_u16le(&bytes[2])) & 0x0fffU;
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    std::uint16_t get_nsync() const noexcept {
        return internal::read_u16le(&bytes[0]);
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    bool is_special() const noexcept { return get_channel() == 15; }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    bool is_nsync_overflow() const noexcept {
        return is_special() && get_dtime() == 0;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    std::uint16_t get_nsync_overflow_count() const noexcept { return 1; }

    /**
     * \brief Determine if this event represents markers.
     */
    bool is_external_marker() const noexcept {
        return is_special() && get_dtime() != 0;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    std::uint16_t get_external_marker_bits() const noexcept {
        return get_dtime();
    }
};

/**
 * \brief Abstract base class for binary record interpretation for HydraHarp,
 * MultiHarp, and TimeHarp260 T3 format.
 *
 * \tparam IsHydraV1 if true, interpret as HydraHarp V1 (RecType 0x00010304)
 * format, in which nsync overflow records always indicate a single overflow
 */
template <bool IsHydraV1> struct pq_hydra_t3_event {
    /**
     * \brief Bytes of the 32-bit raw device event.
     */
    unsigned char bytes[4];

    /**
     * \brief The nsync overflow period of this event type.
     */
    static constexpr macrotime nsync_overflow_period = 1024;

    /**
     * \brief Read the channel if this event represents a photon.
     */
    std::uint8_t get_channel() const noexcept {
        return (unsigned(bytes[3]) & 0x7fU) >> 1;
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    std::uint16_t get_dtime() const noexcept {
        auto lo6 = unsigned(bytes[1]) >> 2;
        auto mid8 = unsigned(bytes[2]);
        auto hi1 = unsigned(bytes[3]) & 1U;
        return lo6 | (mid8 << 6) | (hi1 << 14);
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    std::uint16_t get_nsync() const noexcept {
        return unsigned(internal::read_u16le(&bytes[0])) & 0x03ffU;
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    bool is_special() const noexcept { return unsigned(bytes[3]) & (1U << 7); }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    bool is_nsync_overflow() const noexcept {
        return is_special() && get_channel() == 63;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    std::uint16_t get_nsync_overflow_count() const noexcept {
        if (IsHydraV1 || get_nsync() == 0) {
            return 1;
        }
        return get_nsync();
    }

    /**
     * \brief Determine if this event represents markers.
     */
    bool is_external_marker() const noexcept {
        return is_special() && get_channel() != 63;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    std::uint8_t get_external_marker_bits() const noexcept {
        return get_channel();
    }
};

/**
 * \brief Binary record interpretation for HydraHarp V1 T3 format.
 */
using pq_hydra_v1_t3_event = pq_hydra_t3_event<true>;

/**
 * \brief Binary record interpretation for HydraHarp V2, MultiHarp, and
 * TimeHarp260 T3 format.
 */
using pq_hydra_v2_t3_event = pq_hydra_t3_event<false>;

namespace internal {

// Common implementation for decode_pq_pico_t3, decode_pq_hydra_v1_t3,
// decode_pq_hydra_v2_t3.
// E is the binary record event class.
template <typename E, typename D> class base_decode_pq_t3 {
    macrotime nsync_base;
    macrotime last_nsync;

    D downstream;

  public:
    explicit base_decode_pq_t3(D &&downstream)
        : nsync_base(0), last_nsync(0), downstream(std::move(downstream)) {}

    void handle_event(E const &event) noexcept {
        if (event.is_nsync_overflow()) {
            nsync_base +=
                E::nsync_overflow_period * event.get_nsync_overflow_count();

            time_reached_event e;
            e.macrotime = nsync_base;
            downstream.handle_event(e);
            return;
        }

        macrotime nsync = nsync_base + event.get_nsync();

        // Validate input: ensure nsync increases monotonically (a common
        // assumption made by downstream processors)
        if (nsync <= last_nsync) {
            downstream.handle_end(std::make_exception_ptr(
                std::runtime_error("Non-monotonic nsync encountered")));
            return;
        }
        last_nsync = nsync;

        if (event.is_external_marker()) {
            marker_event e;
            e.macrotime = nsync;
            std::uint32_t bits = event.get_external_marker_bits();
            while (bits) {
                e.channel = internal::count_trailing_zeros_32(bits);
                downstream.handle_event(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        time_correlated_count_event e;
        e.macrotime = nsync;
        e.difftime = event.get_dtime();
        e.channel = event.get_channel();
        downstream.handle_event(e);
    }

    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};
} // namespace internal

/**
 * \brief Processor that decodes PicoQuant PicoHarp T3 events.
 *
 * \see decode_pq_pico_t3()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class decode_pq_pico_t3
    : public internal::base_decode_pq_t3<pq_pico_t3_event, D> {
  public:
    using internal::base_decode_pq_t3<pq_pico_t3_event, D>::base_decode_pq_t3;
};

/**
 * \brief Deduction guide for constructing a decode_pq_pico_t3 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
decode_pq_pico_t3(D &&downstream) -> decode_pq_pico_t3<D>;

/**
 * \brief Processor that decodes PicoQuant HydraHarp V1 T3 events.
 *
 * \see decode_pq_hydra_v1_t3()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class decode_pq_hydra_v1_t3
    : public internal::base_decode_pq_t3<pq_hydra_v1_t3_event, D> {
  public:
    using internal::base_decode_pq_t3<pq_hydra_v1_t3_event,
                                      D>::base_decode_pq_t3;
};

/**
 * \brief Deduction guide for constructing a decode_pq_hydra_v1_t3 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
decode_pq_hydra_v1_t3(D &&downstream) -> decode_pq_hydra_v1_t3<D>;

/**
 * \brief Processor that decodes PicoQuant HydraHarp V2, MultiHarp, and
 * TimeHarp260 T3 events.
 *
 * \see decode_pq_hydra_v2_t3()
 *
 * \tparam D downstream processor type
 */
template <typename D>
class decode_pq_hydra_v2_t3
    : public internal::base_decode_pq_t3<pq_hydra_v2_t3_event, D> {
  public:
    using internal::base_decode_pq_t3<pq_hydra_v2_t3_event,
                                      D>::base_decode_pq_t3;
};

/**
 * \brief Deduction guide for constructing a decode_pq_hydra_v2_t3 processor.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 */
template <typename D>
decode_pq_hydra_v2_t3(D &&downstream) -> decode_pq_hydra_v2_t3<D>;

/**
 * \brief Event set for PicoQuant PicoHarp T3 data stream.
 */
using pq_pico_t3_events = event_set<pq_pico_t3_event>;

/**
 * \brief Event set for PicoQuant HydraHarp V1 T3 data stream.
 */
using pq_hydra_v1_t3_events = event_set<pq_hydra_v1_t3_event>;

/**
 * \brief Event set for PicoQuant HydraHarp V2, MultiHarp, and TimeHarp260 T3
 * data stream.
 */
using pq_hydra_v2_t3_events = event_set<pq_hydra_v2_t3_event>;

} // namespace flimevt
