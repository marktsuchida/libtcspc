/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "EventSet.hpp"

#include <exception>
#include <type_traits>

namespace flimevt {

/**
 * \brief Processor that sinks any event and does nothing.
 */
class DiscardAny {
  public:
    /** \brief Processor interface */
    template <typename E> void HandleEvent(E const &) noexcept {}

    /** \brief Processor interface */
    void HandleEnd(std::exception_ptr) noexcept {}
};

/**
 * \brief Processor that sinks a specified set of events and does nothing.
 *
 * \tparam ESet the set of events to consume.
 */
template <typename ESet> class DiscardAll {
  public:
    /** \brief Processor interface */
    template <typename E, typename = std::enable_if_t<ContainsEventV<ESet, E>>>
    void HandleEvent(E const &) noexcept {}

    /** \brief Processor interface */
    void HandleEnd(std::exception_ptr) noexcept {}
};

} // namespace flimevt
