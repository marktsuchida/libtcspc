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

} // namespace tcspc
