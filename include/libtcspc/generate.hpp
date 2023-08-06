/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <cstddef>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace tcspc {

namespace internal {

template <typename TriggerEvent, typename TimingGenerator, typename Downstream>
class generate {
    using abstime_type = typename TimingGenerator::abstime_type;
    static_assert(
        std::is_same_v<abstime_type,
                       decltype(std::declval<TriggerEvent>().abstime)>);

    TimingGenerator generator;
    Downstream downstream;

    // Pred: bool(abstime_type const &)
    template <typename Pred> void emit(Pred predicate) {
        for (std::optional<abstime_type> t = std::as_const(generator).peek();
             t && predicate(*t); t = std::as_const(generator).peek()) {
            typename TimingGenerator::output_event_type e = generator.pop();
            downstream.handle(e);
        }
    }

  public:
    explicit generate(TimingGenerator &&generator, Downstream &&downstream)
        : generator(std::move(generator)), downstream(std::move(downstream)) {}

    void handle(TriggerEvent const &event) {
        emit([now = event.abstime](auto t) { return t < now; });
        generator.trigger(event.abstime);
        downstream.handle(event);
    }

    template <typename OtherTimeTaggedEvent>
    void handle(OtherTimeTaggedEvent const &event) {
        emit([now = event.abstime](auto t) { return t <= now; });
        downstream.handle(event);
    }

    void flush() {
        // Note that we do _not_ generate the remaining timings. Usually timing
        // events beyond the end of the event stream are not useful, and not
        // generating them means that infinite generators can be used.
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a processor that generates a pattern of timing events in
 * response to a trigger.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Every time a \c TriggerEvent is received, generation of a pattern of timing
 * events is started according to the given \e generator.
 *
 * Timing events are generated just before an event with an equal or greater
 * abstime is passed through. In particular, timing events beyond the
 * last-passed-through event are not generated.
 *
 * If the next \c TriggerEvent is received before the current pattern has been
 * completed, any remaining timing events in the pattern are suppressed
 * (including any that would have had the same abstime as the \c
 * TriggerEvent).
 *
 * (If a timing event generated by the previous trigger shares the same
 * abstime as a new trigger, it will only be emitted if some other event
 * (also with the same abstime) is passed through before the new trigger.
 * This usually makes sense when the generated events are conceptually some
 * kind of subdivision of the trigger interval. In most applications, however,
 * it is expected that the next trigger is not received until a later abstime
 * after all the timing events in the previous series have been generated.)
 *
 * The type \c TimingGenerator must have the following members:
 *
 * - <tt>abstime_type</tt>, the integer type for absolute time,
 *
 * - <tt>output_event_type</tt>, the type of the generated event (which must
 * have a \c abstime data member of type \c abstime_type),
 *
 * - <tt>void trigger(abstime_type starttime) noexcept</tt>, which starts a new
 * iteration of timing generation,
 *
 * - <tt>auto peek() const noexcept -> std::optional<abstime_type></tt>, which
 * returns the abstime of the next event to be generated, if any, and
 *
 * - <tt>auto pop() noexcept -> output_event_type</tt>, which generates the
 * next event.
 *
 * It is guaranteed that \c pop is only called when \c peek returns a
 * abstime. However, \c peek must return the correct value (\c std::nullopt)
 * even if \c trigger has not yet been called.  In other words, the generator
 * must not produce any events before the first time it is triggered.
 *
 * For timing generators provided by libtcspc, see \ref timing-generators.
 *
 * \tparam TriggerEvent even type that triggers a new round of timing
 * generation by resetting the timing generator
 *
 * \tparam TimingGenerator timing generator type
 *
 * \tparam Downstream downstream processor type
 *
 * \param generator the timing generator
 *
 * \param downstream downstream processor
 *
 * \return generate-timings processor
 *
 * \inevents
 * \event{TriggerEvent, starts timing generation; passed through}
 * \event{All other events, passed through}
 * \endevents
 *
 * \outevents
 * \event{TimingGenerator::output_event_type, emitted at times relative to
 * \c TriggerEvent}
 * \event{TriggerEvent, passed through}
 * \event{Other events, passed through}
 * \endevents
 */
template <typename TriggerEvent, typename TimingGenerator, typename Downstream>
auto generate(TimingGenerator &&generator, Downstream &&downstream) {
    return internal::generate<TriggerEvent, TimingGenerator, Downstream>(
        std::forward<TimingGenerator>(generator),
        std::forward<Downstream>(downstream));
}

/**
 * \brief Timing generator that generates no output events.
 *
 * \ingroup timing-generators
 *
 * Timing generator for use with \ref generate.
 *
 * \tparam Event output event type (never generated)
 */
template <typename Event> class null_timing_generator {
  public:
    /** \brief Timing generator interface */
    using abstime_type = decltype(std::declval<Event>().abstime);

    /** \brief Timing generator interface */
    using output_event_type = Event;

    /** \brief Timing generator interface */
    void trigger(abstime_type starttime) noexcept { (void)starttime; }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const noexcept -> std::optional<abstime_type> {
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
 * Timing generator for use with \ref generate.
 *
 * \tparam Event output event type
 */
template <typename Event> class one_shot_timing_generator {
  public:
    /** \brief Timing generator interface */
    using abstime_type = decltype(std::declval<Event>().abstime);

  private:
    bool pending = false;
    abstime_type next = 0;
    abstime_type delay;

  public:
    /** \brief Timing generator interface */
    using output_event_type = Event;

    /**
     * \brief Construct with delay.
     *
     * \param delay how much to delay the output event relative to the trigger
     */
    explicit one_shot_timing_generator(abstime_type delay) : delay(delay) {
        if (delay < 0)
            throw std::invalid_argument(
                "one_shot_timing_generator delay must not be negative");
    }

    /** \brief Timing generator interface */
    void trigger(abstime_type starttime) noexcept {
        next = starttime + delay;
        pending = true;
    }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const noexcept -> std::optional<abstime_type> {
        if (pending)
            return next;
        return std::nullopt;
    }

    /** \brief Timing generator interface */
    auto pop() noexcept -> Event {
        Event event;
        event.abstime = next;
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
 * Timing generator for use with \ref generate.
 *
 * \tparam Event output event type
 */
template <typename Event> class linear_timing_generator {
  public:
    /** \brief Timing generator interface */
    using abstime_type = decltype(std::declval<Event>().abstime);

  private:
    abstime_type next = 0;
    std::size_t remaining = 0;

    abstime_type delay;
    abstime_type interval;
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
    explicit linear_timing_generator(abstime_type delay, abstime_type interval,
                                     std::size_t count)
        : delay(delay), interval(interval), count(count) {
        if (delay < 0)
            throw std::invalid_argument(
                "linear_timing_generator delay must not be negative");
        if (interval <= 0)
            throw std::invalid_argument(
                "linear_timing_generator interval must be positive");
    }

    /** \brief Timing generator interface */
    void trigger(abstime_type starttime) noexcept {
        next = starttime + delay;
        remaining = count;
    }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const noexcept -> std::optional<abstime_type> {
        if (remaining > 0)
            return next;
        return std::nullopt;
    }

    /** \brief Timing generator interface */
    auto pop() noexcept -> Event {
        Event event;
        event.abstime = next;
        next += interval;
        --remaining;
        return event;
    }
};

} // namespace tcspc
