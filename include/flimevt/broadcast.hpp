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
template <typename... Ds> class broadcast {
    std::tuple<Ds...> downstreams;

  public:
    /**
     * \brief Construct with downstream processors.
     *
     * \param downstreams downstream processors (moved out)
     */
    explicit broadcast(Ds &&...downstreams)
        : downstreams{std::move<Ds>(downstreams)...} {}

    /** \brief Processor interface */
    template <typename E> void handle_event(E const &event) noexcept {
        std::apply([&](auto &...s) { (..., s.handle_event(event)); },
                   downstreams);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        std::apply([&](auto &...s) { (..., s.handle_end(error)); },
                   downstreams);
    }
};

} // namespace flimevt
