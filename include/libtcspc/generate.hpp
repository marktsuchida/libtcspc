/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "data_types.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename TriggerEvent, typename OutputEvent,
          typename TimingGenerator, typename Downstream>
class generate {
    static_assert(is_processor_v<Downstream, TriggerEvent, OutputEvent>);

    using abstime_type = decltype(std::declval<TriggerEvent>().abstime);
    static_assert(
        std::is_same_v<std::remove_reference_t<
                           decltype(*std::declval<TimingGenerator>().peek())>,
                       abstime_type>);
    static_assert(std::is_same_v<decltype(std::declval<OutputEvent>().abstime),
                                 abstime_type>);

    TimingGenerator generator;

    Downstream downstream;

    // Pred: bool(abstime_type const &)
    template <typename Pred> void emit(Pred predicate) {
        auto const &const_gen = generator; // Enforce peek() const.
        for (std::optional<abstime_type> t = const_gen.peek();
             t && predicate(*t); t = const_gen.peek()) {
            OutputEvent event;
            event.abstime = *t;
            generator.pop();
            downstream.handle(std::move(event));
        }
    }

  public:
    explicit generate(TimingGenerator generator, Downstream downstream)
        : generator(std::move(generator)), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "generate");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E, typename = std::enable_if_t<
                              handles_event_v<Downstream, remove_cvref_t<E>>>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, TriggerEvent>) {
            emit([now = event.abstime](auto t) { return t < now; });
            generator.trigger(event);
        } else {
            emit([now = event.abstime](auto t) { return t <= now; });
        }
        downstream.handle(std::forward<E>(event));
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
 * Every time a \p TriggerEvent is received, generation of a pattern of timing
 * events of type \p OutputEvent is started according to the given \p generator
 * (see \ref timing-generators).
 *
 * Timing events are generated just _before_ an event with an equal or greater
 * abstime is passed through. In particular, timing events beyond the
 * last-passed-through event are not generated.
 *
 * If the next \p TriggerEvent is is received before the current pattern has
 * been completed, any remaining timing events in the pattern are suppressed
 * (including any that would have had the same abstime as the \p TriggerEvent).
 *
 * \note If a timing event generated by the previous trigger shares the same
 * abstime as a new trigger, it will only be emitted if some other event (also
 * with the same abstime) is passed through before the new trigger. This
 * usually makes sense when the generated events are conceptually some kind of
 * subdivision of the trigger interval. In most applications, however, it is
 * expected that the next trigger is not received until a later abstime after
 * all the timing events in the previous series have been generated.
 *
 * \attention The `abstime` of incoming events must be monotonically
 * non-decreasing and must not wrap around.
 *
 * \tparam TriggerEvent event type that triggers a new round of timing
 * generation by resetting the timing generator
 *
 * \tparam OutputEvent event type to generate, which must have an `abstime`
 * field whose type matches that of \p TriggerEvent
 *
 * \tparam TimingGenerator timing generator type (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param generator the timing generator
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `TriggerEvent`: emit any generated `OutputEvent`s based on the previous
 *   trigger and with earlier abstime; discard any remaining `OutputEvent`s
 *   that the previous trigger would have produced; set up new round of
 *   generation; pass through
 * - All other types with `abstime` field: emit any generated events based on
 *   the previous trigger and with earlier or equal abstime; pass through
 * - Flush: passed through without action
 */
template <typename TriggerEvent, typename OutputEvent,
          typename TimingGenerator, typename Downstream>
auto generate(TimingGenerator &&generator, Downstream &&downstream) {
    return internal::generate<TriggerEvent, OutputEvent, TimingGenerator,
                              Downstream>(
        std::forward<TimingGenerator>(generator),
        std::forward<Downstream>(downstream));
}

/**
 * \brief Timing generator that generates no timings.
 *
 * \ingroup timing-generators
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class null_timing_generator {
  public:
    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
        return std::nullopt;
    }

    /** \brief Implements timing generator requirement. */
    void pop() { internal::unreachable(); }
};

/**
 * \brief Timing generator that generates a single, delayed timing.
 *
 * \ingroup timing-generators
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class one_shot_timing_generator {
    std::optional<typename DataTypes::abstime_type> next;
    typename DataTypes::abstime_type dly;

  public:
    /**
     * \brief Construct an instance that generates a timing after \p delay
     * relative to each trigger.
     *
     * \p delay must be nonnegative.
     */
    explicit one_shot_timing_generator(
        arg::delay<typename DataTypes::abstime_type> delay)
        : dly(delay.value) {
        if (dly < 0)
            throw std::invalid_argument(
                "one_shot_timing_generator delay must not be negative");
    }

    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        next = event.abstime + dly;
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
        return next;
    }

    /** \brief Implements timing generator requirement. */
    void pop() { next.reset(); }
};

/**
 * \brief Timing generator that generates a single, delayed timing, configured
 * by the trigger event.
 *
 * \ingroup timing-generators
 *
 * The delay of the output timing (relative to the trigger event) is obtained
 * from the `delay` data member of each trigger event.
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class dynamic_one_shot_timing_generator {
    std::optional<typename DataTypes::abstime_type> next;

  public:
    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<decltype(event.delay),
                                     typename DataTypes::abstime_type>);
        next = event.abstime + event.delay;
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
        return next;
    }

    /** \brief Implements timing generator requirement. */
    void pop() { next.reset(); }
};

/**
 * \brief Timing generator that generates an equally spaced series of timings.
 *
 * \ingroup timing-generators
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class linear_timing_generator {
    typename DataTypes::abstime_type next = 0;
    std::size_t remaining = 0;

    typename DataTypes::abstime_type dly;
    typename DataTypes::abstime_type intval;
    std::size_t ct;

  public:
    /**
     * \brief Construct an instance that generates \p count timings at \p
     * interval after \p delay relative to each trigger.
     *
     * \p delay must be nonnegative; \p interval must be positive.
     */
    explicit linear_timing_generator(
        arg::delay<typename DataTypes::abstime_type> delay,
        arg::interval<typename DataTypes::abstime_type> interval,
        arg::count<std::size_t> count)
        : dly(delay.value), intval(interval.value), ct(count.value) {
        if (dly < 0)
            throw std::invalid_argument(
                "linear_timing_generator delay must not be negative");
        if (intval <= 0)
            throw std::invalid_argument(
                "linear_timing_generator interval must be positive");
    }

    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        next = event.abstime + dly;
        remaining = ct;
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
        if (remaining > 0)
            return next;
        return std::nullopt;
    }

    /** \brief Implements timing generator requirement. */
    void pop() {
        next += intval;
        --remaining;
    }
};

/**
 * \brief Timing generator that generates an equally spaced series of timings,
 * configured by the trigger event.
 *
 * \ingroup timing-generators
 *
 * The configuration of output timings is obtained from the `delay`,
 * `interval`, and `count` data members of each trigger event.
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class dynamic_linear_timing_generator {
    typename DataTypes::abstime_type next = 0;
    std::size_t remaining = 0;
    typename DataTypes::abstime_type interval = 0;

  public:
    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        next = event.abstime + event.delay;
        remaining = event.count;
        interval = event.interval;
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
        if (remaining > 0)
            return next;
        return std::nullopt;
    }

    /** \brief Implements timing generator requirement. */
    void pop() {
        next += interval;
        --remaining;
    }
};

} // namespace tcspc
