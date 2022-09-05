#pragma once

#include "Common.hpp"

#include <cassert>
#include <cstddef>
#include <exception>
#include <utility>

namespace flimevt {

/**
 * \brief Processor that generates timing events in response to a trigger.
 *
 * All events are passed through.
 *
 * Every time an \c ETrig is received, generation of a pattern of timing events
 * is started according to the given pattern generator.
 *
 * Timing events are only generated when an event with a greater macrotime is
 * passed through. In particular, timing events beyond the last passed through
 * event are not generated.
 *
 * If the next \c ETrig is received before pattern generation has finished, any
 * remaining timing events are not generated.
 *
 * The pattern generator type \c PGen must have the following members:
 * - <tt>OutputEventType</tt>, the type of the generated event,
 * - <tt>void Trigger(Macrotime starttime) noexcept</tt>, which starts a new
 *   iteration of pattern generation,
 * - <tt>bool Peek(Macrotime &macrotime) const noexcept</tt>, which returns
 *   whether there are remaining events to be generated, and sets the argument
 *   to the macrotime of the next event to be generated, and
 * - <tt>void Pop(OutputEventType &event) noexcept</tt>, which generates the
 *   next event into the argument.
 *
 * \tparam ETrig even type that triggers a new round of pattern generation by
 * resetting the pattern generator
 * \tparam PGen timing pattern generator type
 * \tparam D downstream processor type
 */
template <typename ETrig, typename PGen, typename D> class GenerateTimings {
    PGen generator;
    D downstream;

    // P: bool(Macrotime const &)
    template <typename P> void Emit(P predicate) noexcept {
        Macrotime t;
        while (generator.Peek(t) && predicate(t)) {
            typename PGen::OutputEventType e{};
            generator.Pop(e);
            downstream.HandleEvent(e);
        }
    }

  public:
    /**
     * \brief Construct with pattern generator and downstream.
     *
     * The generator must be in a state where it generates no events until the
     * next trigger.
     *
     * \param generator the timing pattern generator (moved out)
     * \param downstream downstream processor (moved out)
     */
    explicit GenerateTimings(PGen &&generator, D &&downstream)
        : generator(std::move(generator)), downstream(std::move(downstream)) {}

    void HandleEvent(ETrig const &event) noexcept {
        Emit([&event](Macrotime t) { return t < event.macrotime; });
        generator.Trigger(event.macrotime);
        downstream.HandleEvent(event);
    }

    template <typename E> void HandleEvent(E const &event) noexcept {
        Emit([&event](Macrotime t) { return t <= event.macrotime; });
        downstream.HandleEvent(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        // Note that we do _not_ generate the remaining timings. Usually timing
        // events beyond the end of the event stream are not useful, and not
        // generating them means that infinite generators can be used.
        downstream.HandleEnd(error);
    }
};

/**
 * \brief Timing generator that generates no output events.
 *
 * Timing pattern generator for use with GenerateTimings.
 *
 * \tparam EOut output event type (never generated)
 */
template <typename EOut> class NullTimingGenerator {
  public:
    using OutputEventType = EOut;

    void Trigger(Macrotime starttime) noexcept { (void)starttime; }

    bool Peek(Macrotime &macrotime) const noexcept {
        (void)macrotime;
        return false;
    }

    void Pop(EOut &event) noexcept {
        (void)event;
        assert(false);
    }
};

/**
 * \brief Timing generator that generates a single, delayed output event.
 *
 * Timing pattern generator for use with GenerateTimings.
 *
 * \tparam EOut output event type
 */
template <typename EOut> class OneShotTimingGenerator {
    bool pending = false;
    Macrotime next = 0;
    Macrotime const delay;

  public:
    using OutputEventType = EOut;

    /**
     * \brief Construct with delay.
     *
     * \param delay how much to delay the output event relative to the trigger
     */
    explicit OneShotTimingGenerator(Macrotime delay) : delay(delay) {
        assert(delay >= 0);
    }

    void Trigger(Macrotime starttime) noexcept {
        next = starttime + delay;
        pending = true;
    }

    bool Peek(Macrotime &macrotime) const noexcept {
        macrotime = next;
        return pending;
    }

    void Pop(EOut &event) noexcept {
        event.macrotime = next;
        pending = false;
    }
};

/**
 * \brief Timing generator that generates an equally spaced series of output
 * events.
 *
 * Timing pattern generator for use with GenerateTimings.
 *
 * \tparam EOut output event type
 */
template <typename EOut> class LinearTimingGenerator {
    Macrotime next;
    std::size_t remaining;

    Macrotime const delay;
    Macrotime const interval;
    std::size_t const count;

  public:
    using OutputEventType = EOut;

    /**
     * \brief Construct with delay, interval, and count.
     *
     * \param delay how much to delay the first output event relative to the
     * trigger (must be nonnegative)
     * \param interval time interval between subsequent output events (must be
     * positive)
     * \param count number of output events to generate for each trigger
     */
    explicit LinearTimingGenerator(Macrotime delay, Macrotime interval,
                                   std::size_t count)
        : next(0), remaining(0), delay(delay), interval(interval),
          count(count) {
        assert(delay >= 0);
        assert(interval > 0);
    }

    void Trigger(Macrotime starttime) noexcept {
        next = starttime + delay;
        remaining = count;
    }

    bool Peek(Macrotime &macrotime) const noexcept {
        macrotime = next;
        return remaining > 0;
    }

    void Pop(EOut &event) noexcept {
        event.macrotime = next;
        next += interval;
        --remaining;
    }
};

} // namespace flimevt
