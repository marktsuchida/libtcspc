/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "introspect.hpp"

#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Downstream> class shared_processor {
    std::shared_ptr<Downstream> downstream; // Not null.

  public:
    explicit shared_processor(std::shared_ptr<Downstream> downstream)
        : downstream(std::move(downstream)) {
        // If we allow null, we need to either check for null on every event
        // (unnecessary overhead) or substitute with a null sink (not possible
        // without type erasure, which is out of scope here). So use a narrow
        // contract (behavior is undefined for null downstream).
        if (not this->downstream)
            throw std::invalid_argument(
                "shared_processor downstream must not be null");
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "shared_processor");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream->introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename AnyEvent> void handle(AnyEvent const &event) {
        downstream->handle(event);
    }

    void flush() { downstream->flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that forwards to downstream via a shared pointer.
 *
 * \ingroup processors-basic
 *
 * This is an adapter to allow the use of a downstream processor held by \c
 * std::shared_ptr. It can be used to retain access to the downstream
 * processor after it is attached to the upstream.
 *
 * Copying a shared processor preserves the reference to the same downstream
 * processor, instead of copying the entire downstream as with regular
 * processors. Only one copy should be used to actually receive input.
 *
 * \see ref_processor
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream shared pointer to downstream processor (must not be null)
 *
 * \inevents
 * \event{All events, passed through}
 * \endevents
 *
 * \outevents
 * \event{All events, passed through}
 * \endevents
 */
template <typename Downstream>
auto shared_processor(std::shared_ptr<Downstream> downstream) {
    return internal::shared_processor<Downstream>(std::move(downstream));
}

/**
 * \brief Move-construct an instance managed by \c std::shared_ptr.
 *
 * \ingroup misc
 *
 * This is a helper to make it easier to prepare a processor for use with \ref
 * shared_processor.
 *
 * \tparam T the object type
 *
 * \param t the object
 *
 * \return shared pointer to an instance of \c T move-constructed from \c t.
 */
template <typename T,
          typename = std::enable_if_t<std::is_rvalue_reference_v<T &&>>>
auto move_to_shared(T &&t) -> std::shared_ptr<T> {
    return std::make_shared<T>(std::forward<T>(t));
}

} // namespace tcspc
