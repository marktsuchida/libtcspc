/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "event_set.hpp"

#include <exception>
#include <type_traits>

namespace tcspc {

/**
 * \brief Processor that sinks any event and does nothing.
 */
class discard_any {
  public:
    /** \brief Processor interface */
    template <typename E>
    void handle_event([[maybe_unused]] E const &event) noexcept {}

    /** \brief Processor interface */
    void
    handle_end([[maybe_unused]] std::exception_ptr const &error) noexcept {}
};

/**
 * \brief Processor that sinks a specified set of events and does nothing.
 *
 * \tparam Es the set of events to consume.
 */
template <typename Es> class discard_all {
  public:
    /** \brief Processor interface */
    template <typename E, typename = std::enable_if_t<contains_event_v<Es, E>>>
    void handle_event([[maybe_unused]] E const &event) noexcept {}

    /** \brief Processor interface */
    void
    handle_end([[maybe_unused]] std::exception_ptr const &error) noexcept {}
};

} // namespace tcspc
