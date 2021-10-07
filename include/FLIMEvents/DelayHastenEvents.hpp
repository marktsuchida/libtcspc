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

template <typename DelayedSet, typename D> class DelayEvents {
    Macrotime delta;
    std::queue<EventVariant<DelayedSet>> pending;
    D downstream;
    bool streamEnded = false;

  public:
    explicit DelayEvents(Macrotime delta, D &&downstream)
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

template <typename UnhastenedSet, typename D> class HastenEvents {
    Macrotime delta;
    std::queue<EventVariant<UnhastenedSet>> pending;
    D downstream;
    bool streamEnded = false;

  public:
    explicit HastenEvents(Macrotime delta, D &&downstream)
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
class DelayHastenEvents {
    using Hastener = HastenEvents<UnchangedSet, D>;
    using Delayer = DelayEvents<RetimedSet, Hastener>;
    Delayer proc;

  public:
    explicit DelayHastenEvents(Macrotime delta, D &&downstream)
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
