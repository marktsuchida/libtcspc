/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// Design note: We do not use std::uniform_real_distribution or
// std::generate_canonical, because they may have issues (may return the upper
// bound, depending on implementation). (Also, they do not produce the same
// sequence across library implementations.) Instead, we use our own method to
// produce random doubles in [0.0, 1.0).

// Formality: Check our assumption that 'double' is IEEE 754 double precision.
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<double>::radix == 2);
static_assert(std::numeric_limits<double>::digits == 53);

// Make a uniformly-distributed random double value in [0.0, 1.0), given a
// uniformly-distributed 64-bit random integer r (e.g. from std::mt19937_64).
[[nodiscard]] inline auto uniform_double_0_1(std::uint64_t r) -> double {
    // Keep the random bits in the 52-bit fraction field, but set the sign to
    // positive and exponent to 0 (giving a value in [1.0, 2.0)).
    r &= (1uLL << 52) - 1; // Keep only fraction
    r |= 1023uLL << 52;    // Exponent = 0
    double d{};
    std::memcpy(&d, &r, sizeof(d));
    return d - 1.0; // Will not produce subnormal values.
}

// Make a uniformly-distributed random double value in [0.0, 1.0), given a
// uniformly-distributed 32-bit random integer r from std::minstd_rand.
[[nodiscard]] inline auto uniform_double_0_1_minstd(std::uint32_t r)
    -> double {
    // Since r comes from std::minstd_rand, it is in [1, 2147483646].
    using minstd_type = decltype(std::minstd_rand0());
    static_assert(minstd_type::min() == 1);
    static_assert(minstd_type::max() == 2'147'483'646);
    // Do we care that 0 and 2^31-1 are not included in the range? Probably not
    // for dithering.
    assert(r < 2'147'483'648); // Do allow 0 and 2147483647 in tests.

    // 31 random bits should be plenty for our 1-dimensional dithering
    // purposes. Put the 31 random bits in the most significant part of the
    // 52-bit fraction field; leave the sign positive and exponent to 0 (giving
    // a value in [1.0, 2.0)).
    auto bits = std::uint64_t(r) << (52 - 31);
    bits |= 1023uLL << 52; // Exponent = 0
    double d{};
    std::memcpy(&d, &bits, sizeof(d));
    return d - 1.0; // Will not produce subnormal values.
}

template <typename T>
[[nodiscard]] inline auto dither(double value, double dither_noise) -> T {
    assert(dither_noise >= 0.0);
    assert(dither_noise < 1.0);
    return static_cast<T>(std::floor(value + dither_noise));
}

template <typename T> class dithering_quantizer {
    // Prefer std::minstd_rand() over std::mt19937[_64] because of its compact
    // state (the two have similar performance in a tight loop, but mt19937 has
    // > 2 KiB of state, which can become a nontrivial fraction of the L1D if
    // multiple instances are in use). The "poor" quality of the MINSTD PRNG is
    // likely not a significant issue for dithering purposes.
    // TODO Check if it actually makes any difference.
    std::minstd_rand prng;

  public:
    [[nodiscard]] auto operator()(double value) -> T {
        return dither<T>(value, uniform_double_0_1_minstd(prng()));
    }
};

} // namespace internal

/**
 * \brief Timing generator that generates a single, delayed output event whose
 * abstime is dithered.
 *
 * \ingroup timing-generators
 *
 * Timing generator for use with \ref generate.
 *
 * \tparam Event output event type
 */
template <typename Event> class dithered_one_shot_timing_generator {
  public:
    /** \brief Timing generator interface */
    using abstime_type = decltype(std::declval<Event>().abstime);

    /** \brief Timing generator interface */
    using output_event_type = Event;

  private:
    bool pending = false;
    abstime_type next{};
    double dly;

    internal::dithering_quantizer<abstime_type> dithq;

  public:
    /**
     * \brief Construct with delay.
     *
     * \param delay how much to delay the output event relative to the
     * trigger (must not be negative)
     */
    explicit dithered_one_shot_timing_generator(double delay) : dly(delay) {
        if (delay < 0.0)
            throw std::invalid_argument(
                "dithered one-shot timing generator delay must be non-negative");
    }

    /** \brief Timing generator interface */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);
        pending = true;
        next = event.abstime + dithq(dly);
    }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const -> std::optional<abstime_type> {
        if (pending)
            return next;
        return std::nullopt;
    }

    /** \brief Timing generator interface */
    auto pop() -> Event {
        Event event{};
        event.abstime = next;
        pending = false;
        return event;
    }
};

/**
 * \brief Timing generator that generates a single, delayed output event whose
 * abstime is dithered, configured by the trigger event.
 *
 * \ingroup timing-generators
 *
 * Timing generator for use with \ref generate.
 *
 * The delay of the output event (relative to the trigger event) is obtained
 * from the \c delay data member (type \c double) of each trigger event.
 *
 * \tparam Event output event type
 */
template <typename Event> class dynamic_dithered_one_shot_timing_generator {
  public:
    /** \brief Timing generator interface */
    using abstime_type = decltype(std::declval<Event>().abstime);

    /** \brief Timing generator interface */
    using output_event_type = Event;

  private:
    bool pending = false;
    abstime_type next{};

    internal::dithering_quantizer<abstime_type> dithq;

  public:
    /**
     * \brief Construct.
     */
    explicit dynamic_dithered_one_shot_timing_generator() = default;

    /** \brief Timing generator interface */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);
        pending = true;
        next = event.abstime + dithq(event.delay);
    }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const -> std::optional<abstime_type> {
        if (pending)
            return next;
        return std::nullopt;
    }

    /** \brief Timing generator interface */
    auto pop() -> Event {
        Event event{};
        event.abstime = next;
        pending = false;
        return event;
    }
};

namespace internal {

template <typename Abstime> class dithered_linear_timing_generator_impl {
    Abstime trigger_time{};
    std::size_t remaining = 0;
    Abstime next{};

    double dly;
    double intv;
    std::size_t ct;

    dithering_quantizer<Abstime> dithq;

    void compute_next() {
        if (remaining == 0)
            return;
        auto relnext = dithq(dly + intv * static_cast<double>(ct - remaining));
        if (remaining < ct) { // Clamp to interval floor-ceil.
            auto const relmin =
                next - trigger_time + static_cast<Abstime>(std::floor(intv));
            auto const relmax = relmin + 1;
            relnext = relnext < relmin   ? relmin
                      : relnext > relmax ? relmax
                                         : relnext;
        }
        next = trigger_time + relnext;
    }

  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit dithered_linear_timing_generator_impl(double delay,
                                                   double interval,
                                                   std::size_t count)
        : dly(delay), intv(interval), ct(count) {
        if (delay < 0.0)
            throw std::invalid_argument(
                "dithered timing generator delay must be non-negative");
        if (interval <= 0.0)
            throw std::invalid_argument(
                "dithered timing generator interval must be positive");
    }

    void trigger(Abstime abstime) {
        trigger_time = abstime;
        remaining = ct;
        compute_next();
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void trigger_and_configure(Abstime abstime, double delay, double interval,
                               std::size_t count) {
        dly = delay;
        intv = interval;
        ct = count;
        trigger(abstime);
    }

    [[nodiscard]] auto peek() const -> std::optional<Abstime> {
        if (remaining > 0)
            return next;
        return std::nullopt;
    }

    auto pop() -> Abstime {
        auto const ret = next;
        --remaining;
        compute_next();
        return ret;
    }
};

} // namespace internal

/**
 * \brief Timing generator that generates a periodic series of output events,
 * with temporal dithering.
 *
 * \ingroup timing-generators
 *
 * Timing generator for use with \ref generate.
 *
 * \tparam Event output event type
 */
template <typename Event> class dithered_linear_timing_generator {
  public:
    /** \brief Timing generator interface */
    using abstime_type = decltype(std::declval<Event>().abstime);

    /** \brief Timing generator interface */
    using output_event_type = Event;

  private:
    internal::dithered_linear_timing_generator_impl<abstime_type> impl;

  public:
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
    explicit dithered_linear_timing_generator(double delay, double interval,
                                              std::size_t count)
        : impl(delay, interval, count) {}

    /** \brief Timing generator interface */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);
        impl.trigger(event.abstime);
    }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const -> std::optional<abstime_type> {
        return impl.peek();
    }

    /** \brief Timing generator interface */
    auto pop() -> Event {
        Event event{};
        event.abstime = impl.pop();
        return event;
    }
};

/**
 * \brief Timing generator that generates a periodic series of output events,
 * with temporal dithering, configured by the trigger event.
 *
 * \ingroup timing-generators
 *
 * Timing generator for use with \ref generate.
 *
 * The delay, interval, and count of the output events are obtained from the
 * data members of each trigger event:
 * - \c double \c delay
 * - \c double \c interval
 * - \c std::size_t \c count
 *
 * \tparam Event output event type
 */
template <typename Event> class dynamic_dithered_linear_timing_generator {
  public:
    /** \brief Timing generator interface */
    using abstime_type = decltype(std::declval<Event>().abstime);

    /** \brief Timing generator interface */
    using output_event_type = Event;

  private:
    internal::dithered_linear_timing_generator_impl<abstime_type> impl;

  public:
    /**
     * \brief Construct.
     */
    explicit dynamic_dithered_linear_timing_generator() : impl(0.0, 1.0, 0) {}

    /** \brief Timing generator interface */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime), abstime_type>);
        impl.trigger_and_configure(event.abstime, event.delay, event.interval,
                                   event.count);
    }

    /** \brief Timing generator interface */
    [[nodiscard]] auto peek() const -> std::optional<abstime_type> {
        return impl.peek();
    }

    /** \brief Timing generator interface */
    auto pop() -> Event {
        Event event{};
        event.abstime = impl.pop();
        return event;
    }
};

} // namespace tcspc
