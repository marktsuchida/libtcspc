/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "autocopy_span.hpp"
#include "introspect.hpp"
#include "span.hpp"

#include <cstddef>
#include <exception>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename Event, typename Downstream> class view_as_bytes {
    static_assert(std::is_trivial_v<Event>);
    Downstream downstream;

  public:
    explicit view_as_bytes(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "view_as_bytes");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Event const &event) {
        downstream.handle(
            autocopy_span<std::byte const>(as_bytes(span(&event, 1))));
    }

    void flush() { downstream.flush(); }
};

template <typename Event, typename Downstream>
class view_as_bytes<std::vector<Event>, Downstream> {
    static_assert(std::is_trivial_v<Event>);
    Downstream downstream;

  public:
    explicit view_as_bytes(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "view_as_bytes");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(std::vector<Event> const &event) {
        downstream.handle(
            autocopy_span<std::byte const>(as_bytes(span(event))));
    }

    void flush() { downstream.flush(); }
};

template <typename Event, typename Downstream> class view_histogram_as_bytes {
    Downstream downstream;

  public:
    explicit view_histogram_as_bytes(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "view_histogram_as_bytes");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Event const &event) {
        downstream.handle(autocopy_span(as_bytes(event.histogram.as_span())));
    }

    void flush() { downstream.flush(); }
};

template <typename Event, typename Downstream>
class view_histogram_array_as_bytes {
    Downstream downstream;

  public:
    explicit view_histogram_array_as_bytes(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "view_histogram_array_as_bytes");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Event const &event) {
        downstream.handle(
            autocopy_span(as_bytes(event.histogram_array.as_span())));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that views events to byte spans.
 *
 * \ingroup processors-basic
 *
 * This processor handles events of type \c Event (which must be trivial) and
 * sends them, without copying, to the downstream processor as
 * <tt>autocopy_span<std::byte const></tt>.
 *
 * A specialization is available if \c Event matches \c std::vector<T> where \c
 * T must be trivial. In this case, the span of the vector elements is used.
 *
 * \see write_binary_file
 *
 * \tparam Event event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return view-as-bytes processor
 */
template <typename Event, typename Downstream>
auto view_as_bytes(Downstream &&downstream) {
    return internal::view_as_bytes<Event, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that extracts histogram data as byte spans.
 *
 * \ingroup processors-basic
 *
 * This is similar to \ref view_as_bytes, but rather than viewing the whole
 * event as a byte buffer, extracts the 'histogram' data member (which must be
 * an \ref autocopy_span).
 *
 * \see view_as_bytes
 * \see view_histogram_array_as_bytes
 * \see histogram_event
 * \see concluding_histogram_event
 * \see element_histogram_event
 *
 * \tparam Event histogram event type, which must have an \ref autocopy_span
 * 'histogram' data member
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return view-histogram-as-bytes processor
 */
template <typename Event, typename Downstream>
auto view_histogram_as_bytes(Downstream &&downstream) {
    return internal::view_histogram_as_bytes<Event, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that extracts histogram array data as byte spans.
 *
 * \ingroup processors-basic
 *
 * This is similar to \ref view_as_bytes, but rather than viewing the whole
 * event as a byte buffer, extracts the 'histogram_array' data member (which
 * must be an \ref autocopy_span).
 *
 * \see view_as_bytes
 * \see view_histogram_as_bytes
 * \see histogram_array_event
 * \see concluding_histogram_array_event
 *
 * \tparam Event histogram array event type, which must have an \ref
 * autocopy_span 'histogram_array' data member
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return view-histogram-array-as-bytes processor
 */
template <typename Event, typename Downstream>
auto view_histogram_array_as_bytes(Downstream &&downstream) {
    return internal::view_histogram_array_as_bytes<Event, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
