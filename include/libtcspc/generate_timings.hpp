/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <cassert>
#include <cstddef>
#include <exception>
#include <optional>
#include <utility>

namespace tcspc {

namespace internal {

template <typename TriggerEvent, typename PatternGenerator,
          typename Downstream>
class generate_timings {
    PatternGenerator generator;
    Downstream downstream;

    // Pred: bool(macrotime const &)
    template <typename Pred> void emit(Pred predicate) noexcept {
        for (std::optional<macrotime> t = std::as_const(generator).peek();
             t && predicate(*t); t = std::as_const(generator).peek()) {
            typename PatternGenerator::output_event_type e = generator.pop();
            downstream.handle_event(e);
        }
    }

  public:
    explicit generate_timings(PatternGenerator &&generator,
                              Downstream &&downstream)
        : generator(std::move(generator)), downstream(std::move(downstream)) {}

    void handle_event(TriggerEvent const &event) noexcept {
        emit([now = event.macrotime](auto t) { return t < now; });
        generator.trigger(event.macrotime);
        downstream.handle_event(event);
    }

    template <typename OtherTimeTaggedEvent>
    void handle_event(OtherTimeTaggedEvent const &event) noexcept {
        emit([now = event.macrotime](auto t) { return t <= now; });
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        // Note that we do _not_ generate the remaining timings. Usually timing
        // events beyond the end of the event stream are not useful, and not
        // generating them means that infinite generators can be used.
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that generates timing events in response to a
 * trigger.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Every time an \c TriggerEvent is received, generation of a pattern of timing
 * events is started according to the given pattern generator.
 *
 * Timing events are only generated when an event with a greater macrotime is
 * passed through. In particular, timing events beyond the last passed through
 * event are not generated.
 *
 * If the next \c TriggerEvent is received before pattern generation has
 * finished, any remaining timing events are not generated.
 *
 * The pattern generator type \c PatternGenerator must have the following
 * members:
 *
 * - <tt>output_event_type</tt>, the type of the generated event,
 * - <tt>void trigger(macrotime starttime) noexcept</tt>, which starts a new
 *   iteration of pattern generation,
 * - <tt>bool peek(macrotime &macrotime) const noexcept</tt>, which returns
 *   whether there are remaining events to be generated, and sets the argument
 *   to the macrotime of the next event to be generated, and
 * - <tt>void pop(output_event_type &event) noexcept</tt>, which generates the
 *   next event into the argument.
 *
 * The generator must not produce any events before the first time it is
 * triggered.
 *
 * \tparam TriggerEvent even type that triggers a new round of pattern
 * generation by resetting the pattern generator
 *
 * \tparam PatternGenerator timing pattern generator type
 *
 * \tparam Downstream downstream processor type
 *
 * \param generator the timing pattern generator (moved out)
 *
 * \param downstream downstream processor (moved out)
 *
 * \return a new generate_timings processor
 */
template <typename TriggerEvent, typename PatternGenerator,
          typename Downstream>
auto generate_timings(PatternGenerator &&generator, Downstream &&downstream) {
    return internal::generate_timings<TriggerEvent, PatternGenerator,
                                      Downstream>(
        std::forward<PatternGenerator>(generator),
        std::forward<Downstream>(downstream));
}

/**
 * \brief Timing generator that generates no output events.
 *
 * \ingroup timing-generators
 *
 * Timing pattern generator for use with generate_timings.
 *
 * \tparam Event output event type (never generated)
 */
template <typename Event> class null_timing_generator {
  public:
    /** \brief Timing generator interface */
    using output_event_type = Event;

    /** \brief Timing generator interface */
    void trigger(macrotime starttime) noexcept { (void)starttime; }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const noexcept -> std::optional<macrotime> {
        return std::nullopt;
    }

    /** \brief Timing generator interface */
    auto pop() noexcept -> Event { internal::unreachable(); }
};

/**
 * \brief Timing generator that generates a single, delayed output event.
 *
 * \ingroup timing-generators
 *
 * Timing pattern generator for use with generate_timings.
 *
 * \tparam Event output event type
 */
template <typename Event> class one_shot_timing_generator {
    bool pending = false;
    macrotime next = 0;
    macrotime delay;

  public:
    /** \brief Timing generator interface */
    using output_event_type = Event;

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
    [[nodiscard]] auto peek() const noexcept -> std::optional<macrotime> {
        if (pending)
            return next;
        return std::nullopt;
    }

    /** \brief Timing generator interface */
    auto pop() noexcept -> Event {
        Event event;
        event.macrotime = next;
        pending = false;
        return event;
    }
};

/**
 * \brief Timing generator that generates an equally spaced series of output
 * events.
 *
 * \ingroup timing-generators
 *
 * Timing pattern generator for use with generate_timings.
 *
 * \tparam Event output event type
 */
template <typename Event> class linear_timing_generator {
    macrotime next = 0;
    std::size_t remaining = 0;

    macrotime delay;
    macrotime interval;
    std::size_t count;

  public:
    /** \brief Timing generator interface */
    using output_event_type = Event;

    /**
     * \brief Construct with delay, interval, and count.
     *
     * \param delay how much to delay the first output event relative to the
     * trigger (must be nonnegative)
     *
     * \param interval time interval between subsequent output events (must be
     * positive)
     *
     * \param count number of output events to generate for each trigger
     */
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit linear_timing_generator(macrotime delay, macrotime interval,
                                     std::size_t count)
        : delay(delay), interval(interval), count(count) {
        assert(delay >= 0);
        assert(interval > 0);
    }

    /** \brief Timing generator interface */
    void trigger(macrotime starttime) noexcept {
        next = starttime + delay;
        remaining = count;
    }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const noexcept -> std::optional<macrotime> {
        if (remaining > 0)
            return next;
        return std::nullopt;
    }

    /** \brief Timing generator interface */
    auto pop() noexcept -> Event {
        Event event;
        event.macrotime = next;
        next += interval;
        --remaining;
        return event;
    }
};

} // namespace tcspc
