/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "data_types.hpp"
#include "errors.hpp"

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

// Our dithering is done by adding a triangularly-distributed noise (width 2.0)
// before rounding to the nearest integer. This is the simplest way to keep the
// noise distribution independent of the sample value. (For example, adding a
// uniformly-distributed noise (width 1.0) would not have this property because
// samples closer to integer values would receive noise with a narrower
// distribution after quantization.)

// Design note: We do not use std::uniform_real_distribution or
// std::generate_canonical, because they may have issues (may return the upper
// bound, depending on implementation). (Also, they do not produce the same
// sequence across library implementations.) Instead, we use our own method to
// produce random doubles in [0.0, 1.0).

// We prefer std::minstd_rand() over std::mt19937[_64] because of its compact
// state (the two have similar performance in a tight loop, but mt19937 has > 2
// KiB of state, which can become a nontrivial fraction of the L1D if multiple
// instances are in use). The "poor" quality of the MINSTD PRNG is likely not a
// significant issue for dithering purposes.

// The random integers from std::minstd_rand are in [1, 2147483646].
static_assert(decltype(std::minstd_rand())::min() == 1);
static_assert(decltype(std::minstd_rand())::max() == 2'147'483'646);
// Do we care that 0 and 2^31-1 are not included in the range? Probably not for
// dithering purposes.

// Formality: Check our assumption that 'double' is IEEE 754 double precision.
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<double>::radix == 2);
static_assert(std::numeric_limits<double>::digits == 53);

// Make a uniformly-distributed random double value in [0.0, 1.0), given a
// uniformly-distributed 32-bit random integer r from std::minstd_rand.
[[nodiscard]] inline auto
uniform_double_0_1_minstd(std::uint32_t r) -> double {
    assert(r < 2'147'483'648u); // Do allow 0 and 2147483647 in tests.

    // Put the 31 random bits in the most significant part of the 52-bit
    // fraction field; leave the sign positive and exponent to 0 (giving a
    // value in [1.0, 2.0)).
    auto bits = std::uint64_t(r) << (52 - 31);
    bits |= 1023uLL << 52; // Exponent = 0
    double d{};
    std::memcpy(&d, &bits, sizeof(d));
    return d - 1.0; // Will not produce subnormal values.
}

// Make a triangularly-distributed random double value in (0.0, 2.0), centered
// at 1.0, given two uniformly-distributed 32-bit random integers r0, r1 from
// std::minstd_rand.
[[nodiscard]] inline auto
triangular_double_0_2_minstd(std::uint32_t r0, std::uint32_t r1) -> double {
    auto const d0 = uniform_double_0_1_minstd(r0);
    auto const d1 = uniform_double_0_1_minstd(r1);
    return d0 + (1.0 - d1);
}

// Given noise in [0, 2) (from triangular distribution), return dithered value.
// The return value is in (v - 1.5, v + 1.5).
template <typename T>
[[nodiscard]] inline auto apply_dither(double value,
                                       double dither_noise_0_2) -> T {
    assert(dither_noise_0_2 >= 0.0);
    assert(dither_noise_0_2 < 2.0);
    return static_cast<T>(std::floor(value + dither_noise_0_2 - 0.5));
}

template <typename T> class dithering_quantizer {
    std::minstd_rand prng; // Uses default seed (= 1) for reproducibility.

  public:
    [[nodiscard]] auto operator()(double value) -> T {
        // Ensure r0, r1 are computed in order (for reproducibility).
        auto const r0 = prng();
        auto const r1 = prng();
        return apply_dither<T>(value, triangular_double_0_2_minstd(r0, r1));
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
     * (plus dither) relative to each trigger.
     *
     * \p delay must be at least 1.5 (so that a negative delay does not result
     * from the dithering).
     */
    explicit dithered_one_shot_timing_generator(arg::delay<double> delay)
        : dly(delay.value) {
        if (dly < 1.5)
            throw std::invalid_argument(
                "dithered timing generator delay must be at least 1.5");
    }

    /** \brief Implements timing generator requirement. */
    template <typename TriggerEvent> void trigger(TriggerEvent const &event) {
        static_assert(std::is_same_v<decltype(event.abstime),
                                     typename DataTypes::abstime_type>);
        next = event.abstime + dithq(dly);
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
 * by the trigger event, whose abstime is dithered.
 *
 * \ingroup timing-generators-dither
 *
 * The delay of the output event (relative to the trigger event) is obtained
 * from the `delay` data member (type `double`) of each trigger event
 * (typically `tcspc::real_one_shot_timing_event`). It must be at least 1.5 (so
 * that a negative delay does not result from the dithering).
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
        if (event.delay < 1.5)
            throw data_validation_error(
                "dithered timing generator delay must be at least 1.5");
        next = event.abstime + dithq(event.delay);
    }

    /** \brief Implements timing generator requirement. */
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
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
    double intv{3.0};
    std::size_t ct{};

    dithering_quantizer<Abstime> dithq;

    void compute_next() {
        if (remaining == 0)
            return;
        auto const index = ct - remaining;
        next = trigger_time + dithq(dly + intv * static_cast<double>(index));
    }

  public:
    dithered_linear_timing_generator_impl() = default;

    explicit dithered_linear_timing_generator_impl(
        arg::delay<double> delay, arg::interval<double> interval,
        arg::count<std::size_t> count)
        : dly(delay.value), intv(interval.value), ct(count.value) {
        if (dly < 1.5)
            throw std::invalid_argument(
                "dithered timing generator delay must be at least 1.5");
        if (intv < 3.0)
            throw std::invalid_argument(
                "dithered timing generator interval must be at least 3.0");
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
        if (dly < 1.5)
            throw data_validation_error(
                "dithered timing generator delay must be at least 1.5");
        if (intv < 3.0)
            throw data_validation_error(
                "dithered timing generator interval must be at least 3.0");
        trigger(abstime);
    }

    [[nodiscard]] auto peek() const -> std::optional<Abstime> {
        return remaining > 0 ? next : std::optional<Abstime>{};
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
     * \p delay must be at least 1.5; \p interval must be at least 3.0 (so that
     * a negative delay or interval does not result from the dithering).
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
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
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
 * tcspc::real_linear_timing_event). The delay must be at least 1.5 and the
 * interval must be at least 3.0 (so that a negative delay or interval does not
 * result from the dithering).
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
    [[nodiscard]] auto
    peek() const -> std::optional<typename DataTypes::abstime_type> {
        return impl.peek();
    }

    /** \brief Implements timing generator requirement. */
    void pop() { impl.pop(); }
};

} // namespace tcspc
