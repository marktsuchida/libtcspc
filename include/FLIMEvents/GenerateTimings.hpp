/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

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
 * - <tt>output_event_type</tt>, the type of the generated event,
 * - <tt>void trigger(macrotime starttime) noexcept</tt>, which starts a new
 *   iteration of pattern generation,
 * - <tt>bool peek(macrotime &macrotime) const noexcept</tt>, which returns
 *   whether there are remaining events to be generated, and sets the argument
 *   to the macrotime of the next event to be generated, and
 * - <tt>void pop(output_event_type &event) noexcept</tt>, which generates the
 *   next event into the argument.
 *
 * \tparam ETrig even type that triggers a new round of pattern generation by
 * resetting the pattern generator
 * \tparam PGen timing pattern generator type
 * \tparam D downstream processor type
 */
template <typename ETrig, typename PGen, typename D> class generate_timings {
    PGen generator;
    D downstream;

    // P: bool(macrotime const &)
    template <typename P> void Emit(P predicate) noexcept {
        macrotime t;
        while (generator.peek(t) && predicate(t)) {
            typename PGen::output_event_type e{};
            generator.pop(e);
            downstream.handle_event(e);
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
    explicit generate_timings(PGen &&generator, D &&downstream)
        : generator(std::move(generator)), downstream(std::move(downstream)) {}

    /** \brief Processor interface */
    void handle_event(ETrig const &event) noexcept {
        Emit([now = event.macrotime](auto t) { return t < now; });
        generator.trigger(event.macrotime);
        downstream.handle_event(event);
    }

    /** \brief Processor interface */
    template <typename E> void handle_event(E const &event) noexcept {
        Emit([now = event.macrotime](auto t) { return t <= now; });
        downstream.handle_event(event);
    }

    /** \brief Processor interface */
    void handle_end(std::exception_ptr error) noexcept {
        // Note that we do _not_ generate the remaining timings. Usually timing
        // events beyond the end of the event stream are not useful, and not
        // generating them means that infinite generators can be used.
        downstream.handle_end(error);
    }
};

/**
 * \brief Timing generator that generates no output events.
 *
 * Timing pattern generator for use with generate_timings.
 *
 * \tparam EOut output event type (never generated)
 */
template <typename EOut> class null_timing_generator {
  public:
    /** \brief Timing generator interface */
    using output_event_type = EOut;

    /** \brief Timing generator interface */
    void trigger(macrotime starttime) noexcept { (void)starttime; }

    /** \brief Timing generator interface */
    bool peek(macrotime &macrotime) const noexcept {
        (void)macrotime;
        return false;
    }

    /** \brief Timing generator interface */
    void pop(EOut &event) noexcept {
        (void)event;
        assert(false);
    }
};

/**
 * \brief Timing generator that generates a single, delayed output event.
 *
 * Timing pattern generator for use with generate_timings.
 *
 * \tparam EOut output event type
 */
template <typename EOut> class one_shot_timing_generator {
    bool pending = false;
    macrotime next = 0;
    macrotime const delay;

  public:
    /** \brief Timing generator interface */
    using output_event_type = EOut;

    /**
     * \brief Construct with delay.
     *
     * \param delay how much to delay the output event relative to the trigger
     */
    explicit one_shot_timing_generator(macrotime delay) : delay(delay) {
        assert(delay >= 0);
    }

    /** \brief Timing generator interface */
    void trigger(macrotime starttime) noexcept {
        next = starttime + delay;
        pending = true;
    }

    /** \brief Timing generator interface */
    bool peek(macrotime &macrotime) const noexcept {
        macrotime = next;
        return pending;
    }

    /** \brief Timing generator interface */
    void pop(EOut &event) noexcept {
        event.macrotime = next;
        pending = false;
    }
};

/**
 * \brief Timing generator that generates an equally spaced series of output
 * events.
 *
 * Timing pattern generator for use with generate_timings.
 *
 * \tparam EOut output event type
 */
template <typename EOut> class linear_timing_generator {
    macrotime next;
    std::size_t remaining;

    macrotime const delay;
    macrotime const interval;
    std::size_t const count;

  public:
    /** \brief Timing generator interface */
    using output_event_type = EOut;

    /**
     * \brief Construct with delay, interval, and count.
     *
     * \param delay how much to delay the first output event relative to the
     * trigger (must be nonnegative)
     * \param interval time interval between subsequent output events (must be
     * positive)
     * \param count number of output events to generate for each trigger
     */
    explicit linear_timing_generator(macrotime delay, macrotime interval,
                                     std::size_t count)
        : next(0), remaining(0), delay(delay), interval(interval),
          count(count) {
        assert(delay >= 0);
        assert(interval > 0);
    }

    /** \brief Timing generator interface */
    void trigger(macrotime starttime) noexcept {
        next = starttime + delay;
        remaining = count;
    }

    /** \brief Timing generator interface */
    bool peek(macrotime &macrotime) const noexcept {
        macrotime = next;
        return remaining > 0;
    }

    /** \brief Timing generator interface */
    void pop(EOut &event) noexcept {
        event.macrotime = next;
        next += interval;
        --remaining;
    }
};

} // namespace flimevt
