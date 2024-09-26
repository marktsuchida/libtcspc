/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bucket.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"
#include "span.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Downstream> class view_as_bytes {
    static_assert(is_processor_v<Downstream, bucket<std::byte>>);

    Downstream downstream;

  public:
    explicit view_as_bytes(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "view_as_bytes");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename Event,
              typename = std::enable_if_t<std::is_trivial_v<Event>>>
    void handle(Event const &event) {
        // It is safe to const_cast the bytes because we emit the bucket by
        // const ref.
        auto const_byte_span = as_bytes(span(&event, 1));
        auto const b = ad_hoc_bucket(
            span<std::byte>(const_cast<std::byte *>(const_byte_span.data()),
                            const_byte_span.size()));
        downstream.handle(b);
    }

    template <typename T, typename = std::enable_if_t<std::is_trivial_v<T>>>
    void handle(bucket<T> const &event) {
        // It is safe to const_cast the bytes because we emit the bucket by
        // const ref.
        auto const_byte_span = as_bytes(span(event));
        auto const b = ad_hoc_bucket(
            span<std::byte>(const_cast<std::byte *>(const_byte_span.data()),
                            const_byte_span.size()));
        downstream.handle(b);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that views events to byte spans.
 *
 * \ingroup processors-binary
 *
 * This processor handles events of trivial types or buckets of trivial types
 * and sends them, without copying, to the downstream processor as (const
 * lvalue) `bucket<std::byte>`.
 *
 * \see `tcspc::write_binary_stream()`
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Any trivial type: emit its span as `tcspc::bucket<std::byte>`
 * - `tcspc::bucket<T>`: emit its data span as `tcspc::bucket<std::byte>`
 * - Flush: pass through with no action
 */
template <typename Downstream> auto view_as_bytes(Downstream &&downstream) {
    return internal::view_as_bytes<Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
