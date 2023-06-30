/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
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

template <typename MarkerEvent, typename OutputEvent, typename Downstream>
class translate_marker {
    std::int32_t chan;
    Downstream downstream;

    static_assert(
        std::is_same_v<decltype(OutputEvent{0}.macrotime), macrotime>,
        "OutputEvent must have a macrotime field of type macrotime");
    static_assert(OutputEvent{42}.macrotime == 42,
                  "OutputEvent must be initializeable with macrotime");

  public:
    explicit translate_marker(std::int32_t channel, Downstream &&downstream)
        : chan(channel), downstream(std::move(downstream)) {}

    void handle_event(MarkerEvent const &event) noexcept {
        if (event.channel == chan) {
            OutputEvent e{event.macrotime};
            downstream.handle_event(e);
        } else {
            downstream.handle_event(event);
        }
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
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
 * \ingroup processors-timing
 *
 * This can be used, for example, to convert specific marker events into events
 * representing frame, line, or pixel markers for FLIM. Each instance converts
 * a single marker channel to a single event type.
 *
 * The marker event type \c MarkerEvent must have fields \c macrotime and \c
 * channel.
 *
 * The output event type \c OutputEvent must have a \c macrotime field of type
 * \c macrotime, and must be brace-initializable with a macrotime value (as in
 * \c OutputEvent{123} ).
 *
 * \tparam MarkerEvent marker event type
 *
 * \tparam OutputEvent output event type for matching marker events
 *
 * \tparam Downstream downstream processor type
 *
 * \param channel channel of marker events to convert to OutputEvent events
 *
 * \param downstream downstream processor (moved out)
 *
 * \return translate-marker processor
 */
template <typename MarkerEvent, typename OutputEvent, typename Downstream>
auto translate_marker(std::int32_t channel, Downstream &&downstream) {
    return internal::translate_marker<MarkerEvent, OutputEvent, Downstream>(
        channel, std::forward<Downstream>(downstream));
}

} // namespace tcspc
