/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Common.hpp"
#include "EventSet.hpp"

#include <cassert>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <type_traits>
#include <variant>

//! \cond TO_BE_REMOVED

namespace flimevt {

template <typename DelayedSet, typename D> class delay_processor {
    macrotime delta;
    std::queue<event_variant<DelayedSet>> pending;
    D downstream;
    bool streamEnded = false;

  public:
    explicit delay_processor(macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {
        assert(delta >= 0);
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (streamEnded)
            return;

        if constexpr (contains_event_v<DelayedSet, E>) {
            try {
                E delayed(event);
                delayed.macrotime += delta;
                pending.push(delayed);
            } catch (std::exception const &) {
                downstream.handle_end(std::current_exception());
                streamEnded = true;
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
        if (streamEnded)
            return;

        while (!pending.empty()) {
            std::visit([&](auto const &e) { downstream.handle_event(e); },
                       pending.front());
            pending.pop();
        }

        downstream.handle_end(error);
    }
};

template <typename UnhastenedSet, typename D> class hasten_processor {
    macrotime delta;
    std::queue<event_variant<UnhastenedSet>> pending;
    D downstream;
    bool streamEnded = false;

  public:
    explicit hasten_processor(macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {
        assert(delta >= 0);
    }

    template <typename E> void handle_event(E const &event) noexcept {
        if (streamEnded)
            return;

        if constexpr (contains_event_v<UnhastenedSet, E>) {
            try {
                pending.push(event);
            } catch (std::exception const &) {
                downstream.handle_end(std::current_exception());
                streamEnded = true;
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
        if (streamEnded)
            return;

        while (!pending.empty()) {
            std::visit([&](auto const &e) { downstream.handle_event(e); },
                       pending.front());
            pending.pop();
        }

        downstream.handle_end(error);
    }
};

template <typename RetimedSet, typename UnchangedSet, typename D>
class delay_hasten_processor {
    using Hastener = hasten_processor<UnchangedSet, D>;
    using Delayer = delay_processor<RetimedSet, Hastener>;
    Delayer proc;

  public:
    explicit delay_hasten_processor(macrotime delta, D &&downstream)
        : proc(Delayer(
              delta > 0 ? delta : 0,
              Hastener(delta < 0 ? -delta : 0, std::move(downstream)))){};

    template <typename E> void handle_event(E const &event) noexcept {
        proc.handle_event(event);
    }

    void handle_end(std::exception_ptr error) noexcept {
        proc.handle_end(error);
    }
};

} // namespace flimevt

//! \endcond
