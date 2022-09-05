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

namespace flimevt {

template <typename DelayedSet, typename D> class DelayProcessor {
    Macrotime delta;
    std::queue<EventVariant<DelayedSet>> pending;
    D downstream;
    bool streamEnded = false;

  public:
    explicit DelayProcessor(Macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {
        assert(delta >= 0);
    }

    template <typename E> void HandleEvent(E const &event) noexcept {
        if (streamEnded)
            return;

        if constexpr (ContainsEventV<DelayedSet, E>) {
            try {
                E delayed(event);
                delayed.macrotime += delta;
                pending.push(delayed);
            } catch (std::exception const &) {
                downstream.HandleEnd(std::current_exception());
                streamEnded = true;
            }
        } else {
            while (!pending.empty() && std::visit(
                                           [&](auto const &e) {
                                               return e.macrotime <=
                                                      event.macrotime;
                                           },
                                           pending.front())) {
                std::visit([&](auto const &e) { downstream.HandleEvent(e); },
                           pending.front());
                pending.pop();
            }

            downstream.HandleEvent(event);
        }
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        if (streamEnded)
            return;

        while (!pending.empty()) {
            std::visit([&](auto const &e) { downstream.HandleEvent(e); },
                       pending.front());
            pending.pop();
        }

        downstream.HandleEnd(error);
    }
};

template <typename UnhastenedSet, typename D> class HastenProcessor {
    Macrotime delta;
    std::queue<EventVariant<UnhastenedSet>> pending;
    D downstream;
    bool streamEnded = false;

  public:
    explicit HastenProcessor(Macrotime delta, D &&downstream)
        : delta(delta), downstream(std::move(downstream)) {
        assert(delta >= 0);
    }

    template <typename E> void HandleEvent(E const &event) noexcept {
        if (streamEnded)
            return;

        if constexpr (ContainsEventV<UnhastenedSet, E>) {
            try {
                pending.push(event);
            } catch (std::exception const &) {
                downstream.HandleEnd(std::current_exception());
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
                std::visit([&](auto const &e) { downstream.HandleEvent(e); },
                           pending.front());
                pending.pop();
            }

            downstream.HandleEvent(hastened);
        }
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        if (streamEnded)
            return;

        while (!pending.empty()) {
            std::visit([&](auto const &e) { downstream.HandleEvent(e); },
                       pending.front());
            pending.pop();
        }

        downstream.HandleEnd(error);
    }
};

template <typename RetimedSet, typename UnchangedSet, typename D>
class DelayHastenProcessor {
    using Hastener = HastenProcessor<UnchangedSet, D>;
    using Delayer = DelayProcessor<RetimedSet, Hastener>;
    Delayer proc;

  public:
    explicit DelayHastenProcessor(Macrotime delta, D &&downstream)
        : proc(Delayer(
              delta > 0 ? delta : 0,
              Hastener(delta < 0 ? -delta : 0, std::move(downstream)))){};

    template <typename E> void HandleEvent(E const &event) noexcept {
        proc.HandleEvent(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        proc.HandleEnd(error);
    }
};

} // namespace flimevt
