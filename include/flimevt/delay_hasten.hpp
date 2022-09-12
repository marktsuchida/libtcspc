/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"

#include <cassert>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <type_traits>
#include <variant>

//! \cond TO_BE_REMOVED

namespace flimevt {

template <typename EsDelayed, typename D> class delay_processor {
    macrotime delta;
    std::queue<event_variant<EsDelayed>> pending;
    D downstream;
    bool stream_ended = false;

  public:
    explicit delay_processor(macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {
        assert(delta >= 0);
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (stream_ended)
            return;

        if constexpr (contains_event_v<EsDelayed, E>) {
            try {
                E delayed(event);
                delayed.macrotime += delta;
                pending.push(delayed);
            } catch (std::exception const &) {
                downstream.handle_end(std::current_exception());
                stream_ended = true;
            }
        } else {
            while (!pending.empty() && std::visit(
                                           [&](auto const &e) {
                                               return e.macrotime <=
                                                      event.macrotime;
                                           },
                                           pending.front())) {
                std::visit([&](auto const &e) { downstream.handle_event(e); },
                           pending.front());
                pending.pop();
            }

            downstream.handle_event(event);
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (stream_ended)
            return;

        while (!pending.empty()) {
            std::visit([&](auto const &e) { downstream.handle_event(e); },
                       pending.front());
            pending.pop();
        }

        downstream.handle_end(error);
    }
};

template <typename EsUnchanged, typename D> class hasten_processor {
    macrotime delta;
    std::queue<event_variant<EsUnchanged>> pending;
    D downstream;
    bool stream_ended = false;

  public:
    explicit hasten_processor(macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {
        assert(delta >= 0);
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (stream_ended)
            return;

        if constexpr (contains_event_v<EsUnchanged, E>) {
            try {
                pending.push(event);
            } catch (std::exception const &) {
                downstream.handle_end(std::current_exception());
                stream_ended = true;
            }
        } else {
            E hastened(event);
            hastened.macrotime -= delta;

            while (!pending.empty() && std::visit(
                                           [&](auto const &e) {
                                               return e.macrotime <
                                                      hastened.macrotime;
                                           },
                                           pending.front())) {
                std::visit([&](auto const &e) { downstream.handle_event(e); },
                           pending.front());
                pending.pop();
            }

            downstream.handle_event(hastened);
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (stream_ended)
            return;

        while (!pending.empty()) {
            std::visit([&](auto const &e) { downstream.handle_event(e); },
                       pending.front());
            pending.pop();
        }

        downstream.handle_end(error);
    }
};

template <typename EsRetimes, typename EsUnchanged, typename D>
class delay_hasten_processor {
    using hastener_type = hasten_processor<EsUnchanged, D>;
    using delayer_type = delay_processor<EsRetimes, hastener_type>;
    delayer_type proc;

  public:
    explicit delay_hasten_processor(macrotime delta, D &&downstream)
        : proc(delayer_type(
              delta > 0 ? delta : 0,
              hastener_type(delta < 0 ? -delta : 0, std::move(downstream)))){};

    template <typename E> void handle_event(E const &event) noexcept {
        proc.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        proc.handle_end(error);
    }
};

} // namespace flimevt

//! \endcond
