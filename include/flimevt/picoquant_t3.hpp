/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <ostream>
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
    std::array<std::byte, 4> bytes;

    /**
     * \brief The nsync overflow period of this event type.
     */
    static constexpr macrotime nsync_overflow_period = 65536;

    /**
     * \brief Read the channel if this event represents a photon.
     */
    [[nodiscard]] auto channel() const noexcept -> std::uint8_t {
        return read_u8(byte_subspan<3, 1>(bytes)) >> 4;
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    [[nodiscard]] auto dtime() const noexcept -> std::uint16_t {
        return read_u16le(byte_subspan<2, 2>(bytes)) & 0x0fffu;
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    [[nodiscard]] auto nsync() const noexcept -> std::uint16_t {
        return read_u16le(byte_subspan<0, 2>(bytes));
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return channel() == 15;
    }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    [[nodiscard]] auto is_nsync_overflow() const noexcept -> bool {
        return is_special() && dtime() == 0;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    [[nodiscard]] static auto nsync_overflow_count() noexcept
        -> std::uint16_t {
        return 1;
    }

    /**
     * \brief Determine if this event represents markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && dtime() != 0;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto external_marker_bits() const noexcept -> std::uint16_t {
        return dtime();
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pq_pico_t3_event const &lhs,
                           pq_pico_t3_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pq_pico_t3_event const &lhs,
                           pq_pico_t3_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, pq_pico_t3_event const &e)
        -> std::ostream & {
        return strm << "pq_pico_t3(channel=" << e.channel()
                    << ", dtime=" << e.dtime() << ", nsync=" << e.nsync()
                    << ")";
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
    std::array<std::byte, 4> bytes;

    /**
     * \brief The nsync overflow period of this event type.
     */
    static constexpr macrotime nsync_overflow_period = 1024;

    /**
     * \brief Read the channel if this event represents a photon.
     */
    [[nodiscard]] auto channel() const noexcept -> std::uint8_t {
        return (read_u8(byte_subspan<3, 1>(bytes)) & 0x7fu) >> 1;
    }

    /**
     * \brief Read the difference time if this event represents a photon.
     */
    [[nodiscard]] auto dtime() const noexcept -> std::uint16_t {
        unsigned const lo6 = read_u8(byte_subspan<1, 1>(bytes)) >> 2;
        unsigned const mid8 = read_u8(byte_subspan<2, 1>(bytes));
        unsigned const hi1 = read_u8(byte_subspan<3, 1>(bytes)) & 1u;
        return static_cast<std::uint16_t>(lo6 | (mid8 << 6) | (hi1 << 14));
    }

    /**
     * \brief Read the nsync counter value (no rollover correction).
     */
    [[nodiscard]] auto nsync() const noexcept -> std::uint16_t {
        return read_u16le(byte_subspan<0, 2>(bytes)) & 0x03ffu;
    }

    /**
     * \brief Determine if this event is a non-photon event.
     */
    [[nodiscard]] auto is_special() const noexcept -> bool {
        return (read_u8(byte_subspan<3, 1>(bytes)) & (1u << 7)) != 0;
    }

    /**
     * \brief Determine if this event represents an nsync overflow.
     */
    [[nodiscard]] auto is_nsync_overflow() const noexcept -> bool {
        return is_special() && channel() == 63;
    }

    /**
     * \brief Read the nsync overflow count if this event represents an nsync
     * overflow.
     */
    [[nodiscard]] auto nsync_overflow_count() const noexcept -> std::uint16_t {
        if (IsHydraV1 || nsync() == 0) {
            return 1;
        }
        return nsync();
    }

    /**
     * \brief Determine if this event represents markers.
     */
    [[nodiscard]] auto is_external_marker() const noexcept -> bool {
        return is_special() && channel() != 63;
    }

    /**
     * \brief Read the marker bits (mask) if this event represents markers.
     */
    [[nodiscard]] auto external_marker_bits() const noexcept -> std::uint8_t {
        return channel();
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(pq_hydra_t3_event const &lhs,
                           pq_hydra_t3_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(pq_hydra_t3_event const &lhs,
                           pq_hydra_t3_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, pq_hydra_t3_event const &e)
        -> std::ostream & {
        auto version = IsHydraV1 ? 1 : 2;
        return strm << "pq_hydra_v" << version
                    << "_t3(special=" << e.is_special()
                    << ", channel=" << e.channel() << ", dtime=" << e.dtime()
                    << ", nsync=" << e.nsync() << ")";
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
                E::nsync_overflow_period * event.nsync_overflow_count();

            time_reached_event e;
            e.macrotime = nsync_base;
            downstream.handle_event(e);
            return;
        }

        macrotime nsync = nsync_base + event.nsync();

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
            std::uint32_t bits = event.external_marker_bits();
            while (bits != 0) {
                e.channel = count_trailing_zeros_32(bits);
                downstream.handle_event(e);
                bits = bits & (bits - 1); // Clear the handled bit
            }
            return;
        }

        time_correlated_count_event e{nsync, event.dtime(), event.channel()};
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
