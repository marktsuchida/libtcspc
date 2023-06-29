/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <ostream>

namespace tcspc {

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
    std::array<std::byte, 16> bytes;

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
    [[nodiscard]] auto type() const noexcept -> tag_type {
        return tag_type(read_u8(byte_subspan<0, 1>(bytes)));
    }

    // bytes[1] is reserved and should be written zero.

    /**
     * \brief Read the missed event count if this is a missed events event.
     */
    [[nodiscard]] auto missed_event_count() const noexcept -> std::uint16_t {
        return read_u16le(byte_subspan<2, 2>(bytes));
    }

    /**
     * \brief Read the channel if this is a time tag or missed events event.
     */
    [[nodiscard]] auto channel() const noexcept -> std::int32_t {
        return read_i32le(byte_subspan<4, 4>(bytes));
    }

    /**
     * \brief Read the time (picoseconds).
     */
    [[nodiscard]] auto time() const noexcept -> std::int64_t {
        return read_i64le(byte_subspan<8, 8>(bytes));
    }

    /** \brief Equality comparison operator. */
    friend auto operator==(swabian_tag_event const &lhs,
                           swabian_tag_event const &rhs) noexcept -> bool {
        return lhs.bytes == rhs.bytes;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(swabian_tag_event const &lhs,
                           swabian_tag_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, swabian_tag_event const &e)
        -> std::ostream & {
        return strm << "swabian_tag(type=" << static_cast<int>(e.type())
                    << ", missed=" << e.missed_event_count()
                    << ", channel=" << e.channel() << ", time=" << e.time()
                    << ")";
    }
};

namespace internal {

template <typename Downstream> class decode_swabian_tags {
    bool had_error = false;
    Downstream downstream;

  public:
    explicit decode_swabian_tags(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    void handle_event(swabian_tag_event const &event) noexcept {
        if (had_error)
            return;

        using tag_type = swabian_tag_event::tag_type;
        switch (event.type()) {
        case tag_type::time_tag: {
            time_tagged_count_event e{
                {event.time()}, narrow<decltype(e.channel)>(event.channel())};
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
            e.macrotime = event.time();
            downstream.handle_event(e);
            break;
        }
        case tag_type::overflow_end: {
            end_lost_interval_event e;
            e.macrotime = event.time();
            downstream.handle_event(e);
            break;
        }
        case tag_type::missed_events: {
            untagged_counts_event e{
                {event.time()}, event.missed_event_count(), 0};
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

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that decodes Swabian Tag events.
 *
 * \tparam Downstream downstream processor type
 * \param downstream downstream processor (moved out)
 * \return decode-swabian-tags processor
 */
template <typename Downstream>
auto decode_swabian_tags(Downstream &&downstream) {
    return internal::decode_swabian_tags<Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
