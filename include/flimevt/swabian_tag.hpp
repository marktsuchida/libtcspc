/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <cstdint>
#include <exception>

namespace flimevt {

// The raw tag stream format (struct Tag) is documented in Swabian's Time
// Tagger C++ API Manual (part of their software download). See the 16-byte
// 'Tag' struct.

/**
 * \brief Binary record interpretation for 16-byte Swabian 'Tag'.
 *
 * This has the same size and memory layout as the 'Tag' struct in the Swabian
 * Time Tagger C++ API.
 */
struct swabian_tag_event {
    /**
     * \brief Bytes of the 16-byte format from Swabian API.
     */
    std::array<unsigned char, 16> bytes;

    /**
     * \brief 8-bit type for the type field.
     */
    enum class tag_type : std::uint8_t {
        time_tag = 0,
        error = 1,
        overflow_begin = 2,
        overflow_end = 3,
        missed_events = 4,
    };

    /**
     * \brief Read the event type.
     */
    [[nodiscard]] auto get_type() const noexcept -> tag_type {
        return tag_type(bytes[0]);
    }

    // bytes[1] is reserved, to be written zero.

    /**
     * \brief Read the missed event count if this is a missed events event.
     */
    [[nodiscard]] auto get_missed_event_count() const noexcept
        -> std::uint16_t {
        return internal::read_u16le(&bytes[2]);
    }

    /**
     * \brief Read the channel if this is a time tag or missed events event.
     */
    [[nodiscard]] auto get_channel() const noexcept -> std::int32_t {
        return internal::read_i32le(&bytes[4]);
    }

    /**
     * \brief Read the time (picoseconds).
     */
    [[nodiscard]] auto get_time() const noexcept -> std::int64_t {
        return internal::read_i64le(&bytes[8]);
    }
};

namespace internal {

template <typename D> class decode_swabian_tags {
    bool had_error = false;
    D downstream;

  public:
    explicit decode_swabian_tags(D &&downstream)
        : downstream(std::move(downstream)) {}

    void handle_event(swabian_tag_event const &event) noexcept {
        if (had_error)
            return;

        using tag_type = swabian_tag_event::tag_type;
        switch (event.get_type()) {
        case tag_type::time_tag: {
            time_tagged_count_event e{
                {event.get_time()},
                narrow<decltype(e.channel)>(event.get_channel())};
            downstream.handle_event(e);
            break;
        }
        case tag_type::error:
            downstream.handle_end(std::make_exception_ptr(
                std::runtime_error("Error tag in input")));
            had_error = true;
            break;
        case tag_type::overflow_begin: {
            begin_lost_interval_event e;
            e.macrotime = event.get_time();
            downstream.handle_event(e);
            break;
        }
        case tag_type::overflow_end: {
            end_lost_interval_event e;
            e.macrotime = event.get_time();
            downstream.handle_event(e);
            break;
        }
        case tag_type::missed_events: {
            untagged_counts_event e{
                {event.get_time()}, event.get_missed_event_count(), 0};
            downstream.handle_event(e);
            break;
        }
        default:
            downstream.handle_end(std::make_exception_ptr(
                std::runtime_error("Unknown Swabian event type")));
            had_error = true;
            break;
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that decodes Swabian Tag events.
 *
 * \tparam D downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-swabian-tags processor
 */
template <typename D> auto decode_swabian_tags(D &&downstream) {
    return internal::decode_swabian_tags<D>(std::forward<D>(downstream));
}

} // namespace flimevt
