/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "data_types.hpp"

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

namespace tcspc {

namespace internal {

// TODO Dithering should add triangular-distributed noise (width 2), not
// uniformly distributed. Otherwise the effective distribution is
// sample-dependent. The distribution can be produced by adding two random
// samples from uniformly distributed [0, 1) (and subtracting 0.5 before taking
// floor).

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
 * \brief Timing generator that generates a single, delayed timing whose
 * abstime is dithered.
 *
 * \ingroup timing-generators-dither
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class dithered_one_shot_timing_generator {
    std::optional<typename DataTypes::abstime_type> next;
    double dly;

    internal::dithering_quantizer<typename DataTypes::abstime_type> dithq;

  public:
    /**
     * \brief Construct an instance that generates a timing after \p delay
     * relative to each trigger.
     *
     * \p delay must not be negative.
     */
    explicit dithered_one_shot_timing_generator(arg::delay<double> delay)
        : dly(delay.value) {
        if (dly < 0.0)
            throw std::invalid_argument(
                "dithered one-shot timing generator delay must be non-negative");
    }

    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        next = event.abstime + dithq(dly);
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto peek() const
        -> std::optional<typename DataTypes::abstime_type> {
        return next;
    }

    /** \brief Implements timing generator requirement. */
    void pop() { next.reset(); }
};

/**
 * \brief Timing generator that generates a single, delayed timing, configured
 * by the trigger event, whose abstime is dithered.
 *
 * \ingroup timing-generators-dither
 *
 * The delay of the output event (relative to the trigger event) is obtained
 * from the `delay` data member (type `double`) of each trigger event
 * (typically `tcspc::real_one_shot_timing_event`).
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class dynamic_dithered_one_shot_timing_generator {
    std::optional<typename DataTypes::abstime_type> next;

    internal::dithering_quantizer<typename DataTypes::abstime_type> dithq;

  public:
    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        static_assert(std::is_same_v<decltype(event.delay), double>);
        next = event.abstime + dithq(event.delay);
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto peek() const
        -> std::optional<typename DataTypes::abstime_type> {
        return next;
    }

    /** \brief Implements timing generator requirement. */
    void pop() { next.reset(); }
};

namespace internal {

template <typename Abstime> class dithered_linear_timing_generator_impl {
    Abstime trigger_time{};
    std::size_t remaining = 0;
    Abstime next{};

    double dly{};
    double intv{1.0};
    std::size_t ct{};

    dithering_quantizer<Abstime> dithq;

    void compute_next() {
        if (remaining == 0)
            return;
        auto relnext = dithq(dly + intv * static_cast<double>(ct - remaining));
        if (remaining < ct) { // Clamp to interval floor-ceil.
            auto const relmin =
                next - trigger_time + static_cast<Abstime>(std::floor(intv));
            auto const relmax = relmin + 1;
            if (relnext < relmin)
                relnext = relmin;
            else if (relnext > relmax)
                relnext = relmax;
        }
        next = trigger_time + relnext;
    }

  public:
    dithered_linear_timing_generator_impl() = default;

    explicit dithered_linear_timing_generator_impl(
        arg::delay<double> delay, arg::interval<double> interval,
        arg::count<std::size_t> count)
        : dly(delay.value), intv(interval.value), ct(count.value) {
        if (dly < 0.0)
            throw std::invalid_argument(
                "dithered timing generator delay must be non-negative");
        if (intv <= 0.0)
            throw std::invalid_argument(
                "dithered timing generator interval must be positive");
    }

    void trigger(arg::abstime<Abstime> abstime) {
        trigger_time = abstime.value;
        remaining = ct;
        compute_next();
    }

    void trigger_and_configure(arg::abstime<Abstime> abstime,
                               arg::delay<double> delay,
                               arg::interval<double> interval,
                               arg::count<std::size_t> count) {
        dly = delay.value;
        intv = interval.value;
        ct = count.value;
        trigger(abstime);
    }

    [[nodiscard]] auto peek() const -> std::optional<Abstime> {
        if (remaining > 0)
            return next;
        return std::nullopt;
    }

    void pop() {
        --remaining;
        compute_next();
    }
};

} // namespace internal

/**
 * \brief Timing generator that generates a periodic series of timings, with
 * temporal dithering.
 *
 * \ingroup timing-generators-dither
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class dithered_linear_timing_generator {
    internal::dithered_linear_timing_generator_impl<
        typename DataTypes::abstime_type>
        impl;

  public:
    /**
     * \brief Construct an instance that generates \p count timings at \p
     * interval after \p delay relative to each trigger.
     *
     * \p delay must be nonnegative; \p interval must be positive.
     */
    explicit dithered_linear_timing_generator(arg::delay<double> delay,
                                              arg::interval<double> interval,
                                              arg::count<std::size_t> count)
        : impl(delay, interval, count) {}

    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        impl.trigger(arg::abstime{event.abstime});
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto peek() const
        -> std::optional<typename DataTypes::abstime_type> {
        return impl.peek();
    }

    /** \brief Implements timing generator requirement. */
    void pop() { impl.pop(); }
};

/**
 * \brief Timing generator that generates a periodic series of timings,
 * configured by the trigger event, with temporal dithering.
 *
 * \ingroup timing-generators-dither
 *
 * The configuration of output timings is obtained from the `delay`,
 * `interval`, and `count` data members of each trigger event (typically
 * tcspc::real_linear_timing_event).
 *
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <typename DataTypes = default_data_types>
class dynamic_dithered_linear_timing_generator {
    internal::dithered_linear_timing_generator_impl<
        typename DataTypes::abstime_type>
        impl;

  public:
    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        impl.trigger_and_configure(
            arg::abstime{event.abstime}, arg::delay{event.delay},
            arg::interval{event.interval}, arg::count{event.count});
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto peek() const
        -> std::optional<typename DataTypes::abstime_type> {
        return impl.peek();
    }

    /** \brief Implements timing generator requirement. */
    void pop() { impl.pop(); }
};

} // namespace tcspc
