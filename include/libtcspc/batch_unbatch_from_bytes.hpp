/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bucket.hpp"
#include "common.hpp"
#include "introspect.hpp"
#include "span.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Event, typename Downstream> class batch_from_bytes {
    static_assert(std::is_trivial_v<Event>);

    std::shared_ptr<bucket_source<Event>> bsource;

    std::size_t bytes_buffered = 0; // < buf.size()
    std::array<std::byte, sizeof(Event)> buf;

    Downstream downstream;

  public:
    explicit batch_from_bytes(
        std::shared_ptr<bucket_source<Event>> buffer_provider,
        Downstream &&downstream)
        : bsource(std::move(buffer_provider)),
          downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "batch_from_bytes");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename ByteSpan> void handle(ByteSpan const &event) {
        auto input_span = span<std::byte const>(event);
        auto const bytes_available = bytes_buffered + input_span.size();
        if (bytes_available < sizeof(Event)) {
            std::copy(input_span.begin(), input_span.end(),
                      span(buf).subspan(bytes_buffered).begin());
            bytes_buffered = bytes_available;
            return;
        }

        auto const batch_size = bytes_available / sizeof(Event);
        auto bucket = bsource->bucket_of_size(batch_size);
        auto const output_span = as_writable_bytes(span(bucket));
        auto const input_bulk =
            input_span.first(output_span.size() - bytes_buffered);
        auto const remainder = input_span.subspan(input_bulk.size());
        auto const output_bulk = output_span.subspan(bytes_buffered);

        std::copy_n(buf.begin(), bytes_buffered, output_span.begin());
        std::copy(input_bulk.begin(), input_bulk.end(), output_bulk.begin());
        std::copy(remainder.begin(), remainder.end(), buf.begin());
        bytes_buffered = remainder.size();

        downstream.handle(std::move(bucket));
    }

    void flush() {
        if (bytes_buffered > 0)
            throw std::runtime_error("excess bytes at end of stream");
        downstream.flush();
    }
};

template <typename Event, typename Downstream> class unbatch_from_bytes {
    static_assert(std::is_trivial_v<Event>);

    std::size_t bytes_buffered = 0; // < sizeof(buf)
    std::array<std::byte, sizeof(Event)> buf;

    Downstream downstream;

  public:
    explicit unbatch_from_bytes(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "unbatch_from_bytes");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename ByteSpan> void handle(ByteSpan const &event) {
        auto input_span = span<std::byte const>(event);
        if (bytes_buffered > 0) {
            auto const available_bytes = bytes_buffered + input_span.size();
            if (available_bytes < sizeof(Event)) {
                std::copy(input_span.begin(), input_span.end(),
                          span(buf).subspan(bytes_buffered).begin());
                bytes_buffered = available_bytes;
                return;
            }
            Event e;
            auto const output_bytes = as_writable_bytes(span(&e, 1));
            auto const bytes_to_fill = sizeof(Event) - bytes_buffered;
            std::copy_n(buf.begin(), bytes_buffered, output_bytes.begin());
            std::copy_n(input_span.begin(), bytes_to_fill,
                        output_bytes.subspan(bytes_buffered).begin());
            downstream.handle(e);
            input_span = input_span.subspan(bytes_to_fill);
        }

        auto const n_whole = input_span.size() / sizeof(Event);
        auto const whole_event_bytes =
            input_span.first(n_whole * sizeof(Event));
        auto const remainder = input_span.subspan(whole_event_bytes.size());

        if (is_aligned<Event>(input_span.data())) {
            auto const *ptr =
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<Event const *>(input_span.data());
            for (Event const &e : span(ptr, n_whole))
                downstream.handle(e);
        } else {
            for (std::size_t i = 0; i < whole_event_bytes.size();
                 i += sizeof(Event)) {
                Event e;
                std::copy_n(whole_event_bytes.subspan(i).begin(),
                            sizeof(Event),
                            as_writable_bytes(span(&e, 1)).begin());
                downstream.handle(e);
            }
        }

        std::copy(remainder.begin(), remainder.end(), buf.begin());
        bytes_buffered = remainder.size();
    }

    void flush() {
        if (bytes_buffered > 0)
            throw std::runtime_error("excess bytes at end of stream");
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that converts batches of bytes into batches of
 * events.
 *
 * \ingroup processors-binary
 *
 * Copies incoming events (which must be a span, vector, or bucket of
 * `std::byte`) into `tcspc::bucket<Event>`, provided by the given \p
 * buffer_provider.
 *
 * Any input bytes that do not make up a whole \p Event are stored and combined
 * with subsequent input.
 *
 * The output bucket size is variable and contains as many events as can be
 * constructed from the buffered bytes and the input event.
 *
 * \see `tcspc::unbatch_from_bytes()`
 *
 * \tparam Event the event type (must be a trivial type)
 *
 * \tparam Downstream downstream processor type
 *
 * \param buffer_provider bucket source providing event buckets
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Contiguous container or span of `std::byte` or `std::byte const`: copy up
 *   to object boundary to `tcspc::bucket<Event>` and emit as batch
 * - Flush: throw `std::runtime_error` if bytes fewer than `sizeof(Event)` are
 *   left over; pass through
 *
 */
template <typename Event, typename Downstream>
auto batch_from_bytes(std::shared_ptr<bucket_source<Event>> buffer_provider,
                      Downstream &&downstream) {
    return internal::batch_from_bytes<Event, Downstream>(
        std::move(buffer_provider), std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that converts batches of bytes into individual
 * events.
 *
 * \ingroup processors-binary
 *
 * The incoming bytes are interpreted as a contiguous stream of \p Event
 * objects, and emitted individually. The emitted events are aligned to
 * `alignof(Event)` even if the input data is not aligned (by copying if
 * necessary).
 *
 * Any input bytes that do not make up a whole \p Event are stored and combined
 * with subsequent input.
 *
 * \see `tcspc::batch_from_bytes()`
 *
 * \tparam Event the event type (must be a trivial type)
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Contiguous container or span of `std::byte` or `std::byte const`:
 *   collect bytes into `Event` objects and emit
 * - Flush: throw `std::runtime_error` if bytes fewer than `sizeof(Event)` are
 *   left over; pass through
 */
template <typename Event, typename Downstream>
auto unbatch_from_bytes(Downstream &&downstream) {
    return internal::unbatch_from_bytes<Event, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
