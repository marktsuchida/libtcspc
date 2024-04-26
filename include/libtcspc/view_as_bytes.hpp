/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bucket.hpp"
#include "introspect.hpp"
#include "span.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Downstream> class view_as_bytes {
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

    template <typename Event> void handle(Event const &event) {
        static_assert(std::is_trivial_v<Event>);
        struct private_storage {};
        auto const b = bucket<Event const>(span(&event, 1), private_storage{})
                           .byte_bucket();
        downstream.handle(b);
    }

    template <typename T> void handle(bucket<T> const &event) {
        if constexpr (std::is_same_v<std::remove_cv_t<T>, std::byte>) {
            downstream.handle(event);
        } else {
            auto const b = event.const_bucket().byte_bucket();
            downstream.handle(b);
        }
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
 * lvalue) `bucket<std::byte const>`.
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
 * - Any trivial type: emit its span as `tcspc::bucket<std::byte const>`
 * - `tcspc::bucket<T>`: emit its data span as `tcspc::bucket<std::byte const>`
 * - Flush: pass through with no action
 */
template <typename Downstream> auto view_as_bytes(Downstream &&downstream) {
    return internal::view_as_bytes<Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
