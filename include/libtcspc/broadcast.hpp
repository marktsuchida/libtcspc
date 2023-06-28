/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <exception>
#include <tuple>
#include <utility>

namespace tcspc {

namespace internal {

template <typename... Ds> class broadcast {
    std::tuple<Ds...> downstreams;

  public:
    explicit broadcast(Ds &&...downstreams)
        : downstreams{std::move<Ds>(downstreams)...} {}

    template <typename E> void handle_event(E const &event) noexcept {
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
 * \tparam Ds downstream processor classes
 * \param downstreams downstream processors (moved out)
 * \return broadcast processor
 */
template <typename... Ds> auto broadcast(Ds &&...downstreams) {
    return internal::broadcast<Ds...>(std::forward<Ds>(downstreams)...);
}

} // namespace tcspc
