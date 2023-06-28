/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "time_tagged_events.hpp"

#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename EMarker, typename EOut, typename D> class translate_marker {
    std::int32_t chan;
    D downstream;

    static_assert(std::is_same_v<decltype(EOut{0}.macrotime), macrotime>,
                  "EOut must have a macrotime field of type macrotime");
    static_assert(EOut{42}.macrotime == 42,
                  "EOut must be initializeable with macrotime");

  public:
    explicit translate_marker(std::int32_t channel, D &&downstream)
        : chan(channel), downstream(std::move(downstream)) {}

    void handle_event(EMarker const &event) noexcept {
        if (event.channel == chan) {
            EOut e{event.macrotime};
            downstream.handle_event(e);
        } else {
            downstream.handle_event(event);
        }
    }

    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that converts marker events with specific channel
 * to a specified event type.
 *
 * This can be used, for example, to convert specific marker events into events
 * representing frame, line, or pixel markers for FLIM. Each instance converts
 * a single marker channel to a single event type.
 *
 * The marker event type \c EMarker must have fields \c macrotime and \c
 * channel.
 *
 * The output event type \c EOut must have a \c macrotime field of type
 * \c macrotime, and must be brace-initializable with a macrotime value (as in
 * \c EOut{123} ).
 *
 * \tparam EMarker marker event type
 * \tparam EOut output event type for matching marker events
 * \tparam D downstream processor type
 * \param channel channel of marker events to convert to EOut events
 * \param downstream downstream processor (moved out)
 * \return translate-marker processor
 */
template <typename EMarker, typename EOut, typename D>
auto translate_marker(std::int32_t channel, D &&downstream) {
    return internal::translate_marker<EMarker, EOut, D>(
        channel, std::forward<D>(downstream));
}

} // namespace tcspc
