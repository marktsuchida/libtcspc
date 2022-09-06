/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <exception>
#include <tuple>
#include <utility>

namespace flimevt {

/**
 * \brief Processor that broadcasts events to multiple downstream processors.
 *
 * \tparam Ds downstream processor classes
 */
template <typename... Ds> class Broadcast {
    std::tuple<Ds...> downstreams;

  public:
    /**
     * \brief Construct with downstream processors.
     *
     * \param downstreams downstream processors (moved out)
     */
    explicit Broadcast(Ds &&...downstreams)
        : downstreams{std::move<Ds>(downstreams)...} {}

    /** \brief Processor interface */
    template <typename E> void HandleEvent(E const &event) noexcept {
        std::apply([&](auto &...s) { (..., s.HandleEvent(event)); },
                   downstreams);
    }

    /** \brief Processor interface */
    void HandleEnd(std::exception_ptr error) noexcept {
        std::apply([&](auto &...s) { (..., s.HandleEnd(error)); },
                   downstreams);
    }
};

} // namespace flimevt
