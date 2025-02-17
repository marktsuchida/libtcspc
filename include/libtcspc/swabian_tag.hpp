/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "core.hpp"
#include "data_types.hpp"
#include "int_arith.hpp"
#include "int_types.hpp"
#include "introspect.hpp"
#include "npint.hpp"
#include "processor_traits.hpp"
#include "read_integers.hpp"
#include "span.hpp"
#include "time_tagged_events.hpp"

#include <array>
#include <cassert>
#include <cstddef>
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
 * \brief Binary record interpretation for 16-byte Swabian time tag.
 *
 * \ingroup events-swabian
 *
 * This has the same size and memory layout as the `Tag` struct in the Swabian
 * Time Tagger C++ API, which is also the format used by the Python API
 * `CustomMeasurement` class.
 */
struct swabian_tag_event {
    /**
     * \brief Bytes of the 16-byte format from Swabian API.
     */
    std::array<std::byte, 16> bytes;

    /**
     * \brief 8-bit type for the type field.
     */
    enum class tag_type : u8 {
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
        return tag_type(read_u8_at<0>(span(bytes)).value());
    }

    // bytes[1] is reserved and should be written zero.

    /**
     * \brief Read the missed event count if this is a missed events event.
     */
    [[nodiscard]] auto missed_event_count() const noexcept -> u16np {
        return read_u16le_at<2>(span(bytes));
    }

    /**
     * \brief Read the channel if this is a time tag or missed events event.
     */
    [[nodiscard]] auto channel() const noexcept -> i32np {
        return read_i32le_at<4>(span(bytes));
    }

    /**
     * \brief Read the time (picoseconds).
     */
    [[nodiscard]] auto time() const noexcept -> i64np {
        return read_i64le_at<8>(span(bytes));
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
    static auto make_time_tag(i64np time,
                              i32np channel) noexcept -> swabian_tag_event {
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
    friend auto operator<<(std::ostream &strm,
                           swabian_tag_event const &e) -> std::ostream & {
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

template <typename DataTypes, typename Downstream> class decode_swabian_tags {
    static_assert(is_processor_v<Downstream, detection_event<DataTypes>,
                                 begin_lost_interval_event<DataTypes>,
                                 end_lost_interval_event<DataTypes>,
                                 lost_counts_event<DataTypes>, warning_event>);

    static_assert(is_type_in_range<typename DataTypes::abstime_type>(i64{0}));
    static_assert(is_type_in_range<typename DataTypes::channel_type>(i32{0}));
    static_assert(is_type_in_range<typename DataTypes::count_type>(u16{0}));

    Downstream downstream;

    LIBTCSPC_NOINLINE
    void handle_coldpath_tag(swabian_tag_event const &event) {
        switch (event.type()) {
            using tag_type = swabian_tag_event::tag_type;
        case tag_type::time_tag:
            assert(false); // Handled in hot path.
        case tag_type::error:
            return downstream.handle(warning_event{"error tag encountered"});
        case tag_type::overflow_begin:
            return downstream.handle(
                begin_lost_interval_event<DataTypes>{event.time().value()});
        case tag_type::overflow_end:
            return downstream.handle(
                end_lost_interval_event<DataTypes>{event.time().value()});
        case tag_type::missed_events:
            return downstream.handle(lost_counts_event<DataTypes>{
                event.time().value(), event.channel().value(),
                event.missed_event_count().value()});
        }

        std::ostringstream stream;
        stream << "unknown event type ("
               << static_cast<
                      std::underlying_type_t<swabian_tag_event::tag_type>>(
                      event.type())
               << ")";
        downstream.handle(warning_event{stream.str()});
    }

  public:
    explicit decode_swabian_tags(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "decode_swabian_tags");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <
        typename Event,
        typename = std::enable_if_t<
            std::is_convertible_v<remove_cvref_t<Event>, swabian_tag_event> ||
            handles_event_v<Downstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<Event>,
                                            swabian_tag_event>) {
            if (event.type() == swabian_tag_event::tag_type::time_tag) {
                downstream.handle(detection_event<DataTypes>{
                    event.time().value(), event.channel().value()});
            } else {
                handle_coldpath_tag(event);
            }
        } else {
            downstream.handle(std::forward<Event>(event));
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that decodes Swabian Tag events.
 *
 * \ingroup processors-swabian
 *
 * \tparam DataTypes data type set specifying `abstime_type` and `channel_type`
 * for the emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::swabian_tag_event`: decode and emit one of
 *   `tcspc::detection_event<DataTypes>`,
 *   `tcspc::begin_lost_interval_event<DataTypes>`,
 *   `tcspc::end_lost_interval_event<DataTypes>`,
 *   `tcspc::lost_counts_event<DataTypes>`, `warning_event` (warning in
 *   the case of an error tag or unknown tag)
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto decode_swabian_tags(Downstream downstream) {
    return internal::decode_swabian_tags<DataTypes, Downstream>(
        std::move(downstream));
}

} // namespace tcspc
