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
    [[nodiscard]] auto get_channel() const noexcept -> std::uint8_t {
        return static_cast<std::uint8_t>(unsigned(bytes[3]) >> 4);
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    [[nodiscard]] auto get_dtime() const noexcept -> std::uint16_t {
        return unsigned(internal::read_u16le(&bytes[2])) & 0x0fffu;
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    [[nodiscard]] auto get_nsync() const noexcept -> std::uint16_t {
        return internal::read_u16le(&bytes[0]);
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return get_channel() == 15;
    }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    [[nodiscard]] auto is_nsync_overflow() const noexcept -> bool {
        return is_special() && get_dtime() == 0;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    [[nodiscard]] static auto get_nsync_overflow_count() noexcept
        -> std::uint16_t {
        return 1;
    }

    /**
     * \brief Determine if this event represents markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && get_dtime() != 0;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto get_external_marker_bits() const noexcept
        -> std::uint16_t {
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
    [[nodiscard]] auto get_channel() const noexcept -> std::uint8_t {
        return (unsigned(bytes[3]) & 0x7fu) >> 1;
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    [[nodiscard]] auto get_dtime() const noexcept -> std::uint16_t {
        auto lo6 = unsigned(bytes[1]) >> 2;
        auto mid8 = unsigned(bytes[2]);
        auto hi1 = unsigned(bytes[3]) & 1u;
        return static_cast<std::uint16_t>(lo6 | (mid8 << 6) | (hi1 << 14));
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    [[nodiscard]] auto get_nsync() const noexcept -> std::uint16_t {
        return unsigned(internal::read_u16le(&bytes[0])) & 0x03ffu;
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return unsigned(bytes[3]) & (1u << 7);
    }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    [[nodiscard]] auto is_nsync_overflow() const noexcept -> bool {
        return is_special() && get_channel() == 63;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    [[nodiscard]] auto get_nsync_overflow_count() const noexcept
        -> std::uint16_t {
        if (IsHydraV1 || get_nsync() == 0) {
            return 1;
        }
        return get_nsync();
    }

    /**
     * \brief Determine if this event represents markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && get_channel() != 63;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto get_external_marker_bits() const noexcept
        -> std::uint8_t {
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
    macrotime nsync_base = 0;
    macrotime last_nsync = 0;

    D downstream;

  public:
    explicit base_decode_pq_t3(D &&downstream)
        : downstream(std::move(downstream)) {}

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
            marker_event e{{nsync}, 0};
            std::uint32_t bits = event.get_external_marker_bits();
            while (bits) {
                e.channel = count_trailing_zeros_32(bits);
                downstream.handle_event(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        time_correlated_count_event e{nsync, event.get_dtime(),
                                      event.get_channel()};
        downstream.handle_event(e);
    }

    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};
} // namespace internal

/**
 * \brief Create a processor that decodes PicoQuant PicoHarp T3 events.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-pq-pico-t3 processor
 */
template <typename D> auto decode_pq_pico_t3(D &&downstream) {
    return internal::base_decode_pq_t3<pq_pico_t3_event, D>(
        std::forward<D>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V1 T3 events.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-pq-hydra-v1-t3 processor
 */
template <typename D> auto decode_pq_hydra_v1_t3(D &&downstream) {
    return internal::base_decode_pq_t3<pq_hydra_v1_t3_event, D>(
        std::forward<D>(downstream));
}

/**
 * \brief Create a processor that decodes PicoQuant HydraHarp V2, MultiHarp,
 * and TimeHarp260 T3 events.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-pq-hydra-v2-t3 processor
 */
template <typename D> auto decode_pq_hydra_v2_t3(D &&downstream) {
    return internal::base_decode_pq_t3<pq_hydra_v2_t3_event, D>(
        std::forward<D>(downstream));
}

} // namespace flimevt
