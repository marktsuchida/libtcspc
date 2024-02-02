/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "npint.hpp"
#include "read_bytes.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <type_traits>

namespace tcspc {

// The raw tag stream format (struct Tag) is documented in Swabian's Time
// Tagger C++ API Manual (part of their software download). See the 16-byte
// 'Tag' struct.

// Design note: swabian_tag_event does not have an alignment requirement. This
// means that tags can be read from, for example, a memory-mapped file that was
// written without consideration of alignment. On platforms where unaligned
// access requires extra instructions, this may add significant overhead.
// However, on the platforms where this code is most likely to be commonly run
// (x86-64 and aarch64), unaligned load/store usually does not require special
// instructions; therefore there is little advantage to enforcing alignment at
// this level, limiting usage scenarios. If the CPU can perform aligned loads
// and stores faster, this will happen automatically when the buffer is aligned
// at run time.

/**
 * \brief Binary record interpretation for 16-byte Swabian 'Tag'.
 *
 * \ingroup events-device
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
        return tag_type(read_u8(byte_subspan<0, 1>(bytes)).value());
    }

    // bytes[1] is reserved and should be written zero.

    /**
     * \brief Read the missed event count if this is a missed events event.
     */
    [[nodiscard]] auto missed_event_count() const noexcept -> u16np {
        return read_u16le(byte_subspan<2, 2>(bytes));
    }

    /**
     * \brief Read the channel if this is a time tag or missed events event.
     */
    [[nodiscard]] auto channel() const noexcept -> i32np {
        return read_i32le(byte_subspan<4, 4>(bytes));
    }

    /**
     * \brief Read the time (picoseconds).
     */
    [[nodiscard]] auto time() const noexcept -> i64np {
        return read_i64le(byte_subspan<8, 8>(bytes));
    }

    /**
     * \brief Make an event representing a time tag.
     *
     * \param time the timestamp
     *
     * \param channel the channel
     *
     * \return event
     */
    static auto make_time_tag(i64np time, i32np channel) noexcept
        -> swabian_tag_event {
        return make_from_fields(tag_type::time_tag, 0_u16np, channel, time);
    }

    /**
     * \brief Make an event representing an error.
     *
     * \param time the timestamp
     *
     * \return event
     */
    static auto make_error(i64np time) noexcept -> swabian_tag_event {
        return make_from_fields(tag_type::error, 0_u16np, 0_i32np, time);
    }

    /**
     * \brief Make an event representing the beginning of an overflow
     * interval.
     *
     * \param time the timestamp
     *
     * \return event
     */
    static auto make_overflow_begin(i64np time) noexcept -> swabian_tag_event {
        return make_from_fields(tag_type::overflow_begin, 0_u16np, 0_i32np,
                                time);
    }

    /**
     * \brief Make an event representing the end of an overflow interval.
     *
     * \param time the timestamp
     *
     * \return event
     */
    static auto make_overflow_end(i64np time) noexcept -> swabian_tag_event {
        return make_from_fields(tag_type::overflow_end, 0_u16np, 0_i32np,
                                time);
    }

    /**
     * \brief Make an event representing a missed event count.
     *
     * \param time the timestamp
     *
     * \param channel the channel
     *
     * \param count number of missed events
     *
     * \return event
     */
    static auto make_missed_events(i64np time, i32np channel,
                                   u16np count) noexcept -> swabian_tag_event {
        return make_from_fields(tag_type::missed_events, count, channel, time);
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

  private:
    static auto make_from_fields(tag_type type, u16np missed, i32np channel,
                                 i64np time) noexcept -> swabian_tag_event {
        return swabian_tag_event{{
            std::byte(type),
            std::byte(0), // Padding
            std::byte(u8np(missed).value()),
            std::byte(u8np(missed >> 8).value()),
            std::byte(u8np(channel).value()),
            std::byte(u8np(channel >> 8).value()),
            std::byte(u8np(channel >> 16).value()),
            std::byte(u8np(channel >> 24).value()),
            std::byte(u8np(time).value()),
            std::byte(u8np(time >> 8).value()),
            std::byte(u8np(time >> 16).value()),
            std::byte(u8np(time >> 24).value()),
            std::byte(u8np(time >> 32).value()),
            std::byte(u8np(time >> 40).value()),
            std::byte(u8np(time >> 48).value()),
            std::byte(u8np(time >> 56).value()),
        }};
    }
};

namespace internal {

template <typename DataTraits, typename Downstream> class decode_swabian_tags {
    Downstream downstream;

    LIBTCSPC_NOINLINE
    void handle_coldpath_tag(swabian_tag_event const &event) {
        using tag_type = swabian_tag_event::tag_type;
        switch (event.type()) {
        case tag_type::error:
            downstream.handle(warning_event{"error tag encountered"});
            break;
        case tag_type::overflow_begin:
            downstream.handle(
                begin_lost_interval_event<DataTraits>{{event.time().value()}});
            break;
        case tag_type::overflow_end:
            downstream.handle(
                end_lost_interval_event<DataTraits>{{event.time().value()}});
            break;
        case tag_type::missed_events:
            downstream.handle(untagged_counts_event<DataTraits>{
                {{event.time().value()}, event.channel().value()},
                event.missed_event_count().value()});
            break;
        default: {
            std::ostringstream stream;
            stream << "unknown event type ("
                   << static_cast<
                          std::underlying_type_t<swabian_tag_event::tag_type>>(
                          event.type())
                   << ")";
            downstream.handle(warning_event{stream.str()});
            break;
        }
        }
    }

  public:
    explicit decode_swabian_tags(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "decode_swabian_tags");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(swabian_tag_event const &event) {
        if (event.type() == swabian_tag_event::tag_type::time_tag) {
            downstream.handle(detection_event<DataTraits>{
                {{event.time().value()}, event.channel().value()}});
        } else {
            handle_coldpath_tag(event);
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that decodes Swabian Tag events.
 *
 * \ingroup processors-decode
 *
 * \tparam DataTraits traits type specifying \c abstime_type and \c
 * channel_type for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return decode-swabian-tags processor
 */
template <typename DataTraits = default_data_traits, typename Downstream>
auto decode_swabian_tags(Downstream &&downstream) {
    return internal::decode_swabian_tags<DataTraits, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
