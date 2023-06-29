/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <exception>
#include <tuple>
#include <utility>

namespace tcspc {

namespace internal {

template <typename... Downstreams> class broadcast {
    std::tuple<Downstreams...> downstreams;

  public:
    explicit broadcast(Downstreams &&...downstreams)
        : downstreams{std::move<Downstreams>(downstreams)...} {}

    template <typename AnyEvent>
    void handle_event(AnyEvent const &event) noexcept {
        std::apply([&](auto &...s) { (..., s.handle_event(event)); },
                   downstreams);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        std::apply([&](auto &...s) { (..., s.handle_end(error)); },
                   downstreams);
    }
};

} // namespace internal

/**
 * \brief Create a processor that broadcasts events to multiple downstream
 * processors.
 *
 * \tparam Downstreams downstream processor classes
 * \param downstreams downstream processors (moved out)
 * \return broadcast processor
 */
template <typename... Downstreams>
auto broadcast(Downstreams &&...downstreams) {
    return internal::broadcast<Downstreams...>(
        std::forward<Downstreams>(downstreams)...);
}

} // namespace tcspc
