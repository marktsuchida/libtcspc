/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <exception>
#include <memory>
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
        assert(this->downstream);
    }

    template <typename AnyEvent>
    void handle_event(AnyEvent const &event) noexcept {
        downstream->handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream->handle_end(error);
    }
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

} // namespace tcspc
