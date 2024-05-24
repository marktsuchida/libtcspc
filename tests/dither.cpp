/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/dither.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>

namespace tcspc {

namespace {

using trigger_event = time_tagged_test_event<0>;
using output_event = time_tagged_test_event<1>;

} // namespace

namespace internal {

TEST_CASE("uniformly distributed double") {
    CHECK(uniform_double_0_1_minstd(0) == 0.0);
    CHECK(uniform_double_0_1_minstd(1'073'741'824) == 0.5);
    CHECK(uniform_double_0_1_minstd(2'147'483'647) < 1.0);
    CHECK_THAT(uniform_double_0_1_minstd(2'147'483'647),
               Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("triangularly distributed double") {
    static constexpr u32 imax = 2'147'483'647;

    CHECK(triangular_double_0_2_minstd(0, 0) == 1.0);

    CHECK(triangular_double_0_2_minstd(0, imax) > 0.0);
    CHECK_THAT(triangular_double_0_2_minstd(0, imax),
               Catch::Matchers::WithinAbs(0.0, 1e-9));

    CHECK(triangular_double_0_2_minstd(imax, 0) < 2.0);
    CHECK_THAT(triangular_double_0_2_minstd(imax, 0),
               Catch::Matchers::WithinAbs(2.0, 1e-9));

    CHECK(triangular_double_0_2_minstd(imax, imax) == 1.0);
}

TEST_CASE("dither given noise") {
    CHECK(apply_dither<i64>(0.0, 0.0) == -1);
    CHECK(apply_dither<i64>(0.0, 0.4999) == -1);
    CHECK(apply_dither<i64>(0.0, 0.5) == 0);
    CHECK(apply_dither<i64>(0.0, 1.4999) == 0);
    CHECK(apply_dither<i64>(0.0, 1.5) == 1);
    CHECK(apply_dither<i64>(0.0, 1.9999) == 1);

    CHECK(apply_dither<i64>(0.001, 0.0) == -1);
    CHECK(apply_dither<i64>(0.001, 0.4999) == 0);
    CHECK(apply_dither<i64>(0.001, 0.5) == 0);
    CHECK(apply_dither<i64>(0.001, 1.4999) == 1);
    CHECK(apply_dither<i64>(0.001, 1.9999) == 1);

    CHECK(apply_dither<i64>(0.499, 0.0) == -1);
    CHECK(apply_dither<i64>(0.499, 0.9999) == 0);
    CHECK(apply_dither<i64>(0.499, 1.0) == 0);
    CHECK(apply_dither<i64>(0.499, 1.9999) == 1);

    CHECK(apply_dither<i64>(0.5, 0.0) == 0);
    CHECK(apply_dither<i64>(0.5, 0.9999) == 0);
    CHECK(apply_dither<i64>(0.5, 1.0) == 1);
    CHECK(apply_dither<i64>(0.5, 1.9999) == 1);

    CHECK(apply_dither<i64>(0.501, 0.0) == 0);
    CHECK(apply_dither<i64>(0.501, 0.9999) == 1);
    CHECK(apply_dither<i64>(0.501, 1.0) == 1);
    CHECK(apply_dither<i64>(0.501, 1.9999) == 2);

    CHECK(apply_dither<i64>(0.999, 0.0) == 0);
    CHECK(apply_dither<i64>(0.999, 0.5) == 0);
    CHECK(apply_dither<i64>(0.999, 1.4999) == 1);
    CHECK(apply_dither<i64>(0.999, 1.5) == 1);
    CHECK(apply_dither<i64>(0.999, 1.9999) == 2);

    CHECK(apply_dither<i64>(1.0, 0.0) == 0);
    CHECK(apply_dither<i64>(1.0, 0.4999) == 0);
    CHECK(apply_dither<i64>(1.0, 0.5) == 1);
    CHECK(apply_dither<i64>(1.0, 1.4999) == 1);
    CHECK(apply_dither<i64>(1.0, 1.5) == 2);
    CHECK(apply_dither<i64>(1.0, 1.9999) == 2);
}

TEST_CASE("dithering quantizer") {
    dithering_quantizer<i64> dq;
    for (std::size_t i = 0; i < 10'000; ++i) {
        CHECK(dq(0.0) >= -1);
        CHECK(dq(0.0) <= 1);
        CHECK(dq(0.5) >= 0);
        CHECK(dq(0.5) <= 1);
    }
}

} // namespace internal

// NOLINTBEGIN(bugprone-unchecked-optional-access)

TEST_CASE("dithered one-shot timing generator",
          "[dithered_one_shot_timing_generator]") {
    auto tg = dithered_one_shot_timing_generator(arg::delay{1.5});
    CHECK_FALSE(tg.peek().has_value());
    tg.trigger(trigger_event{42});
    CHECK(tg.peek().has_value());
    auto const t0 = tg.peek().value();
    CHECK(t0 >= 42);
    CHECK(t0 <= 44);
    tg.pop();
    CHECK_FALSE(tg.peek().has_value());
}

TEST_CASE("dynamic dithered one-shot timing generator",
          "[dynamic_dithered_one_shot_timing_generator]") {
    auto tg = dynamic_dithered_one_shot_timing_generator();
    CHECK_FALSE(tg.peek().has_value());
    struct trig_evt {
        i64 abstime;
        double delay;
    };
    tg.trigger(trig_evt{42, 1.5});
    CHECK(tg.peek().has_value());
    auto const t0 = tg.peek().value();
    CHECK(t0 >= 42);
    CHECK(t0 <= 44);
    tg.pop();
    CHECK_FALSE(tg.peek().has_value());
}

TEST_CASE("dithered linear timing generator",
          "[dithered_linear_timing_generator]") {
    // Test that generated timings always fulfill criteria, but also that all
    // possible values are seen within a reasonable number of repetitions.
    // (Note that this test should be reproducible because of our use of a
    // standardized PRNG with the same seed.)
    bool seen_t0_43 = false;
    bool seen_t0_44 = false;
    bool seen_t1_53 = false;
    bool seen_t1_54 = false;
    bool seen_t1_55 = false;
    auto tg = dithered_linear_timing_generator(
        arg::delay{1.5}, arg::interval{10.25}, arg::count<std::size_t>{2});
    for (std::size_t i = 0; i < 48; ++i) { // 48 is the sharp minimum to pass.
        CHECK_FALSE(tg.peek().has_value());
        tg.trigger(trigger_event{42});
        CHECK(tg.peek().has_value());
        auto const t0 = tg.peek().value();
        CHECK(t0 >= 43);
        CHECK(t0 <= 44);
        seen_t0_43 = seen_t0_43 || t0 == 43;
        seen_t0_44 = seen_t0_44 || t0 == 44;
        tg.pop();
        CHECK(tg.peek().has_value());
        auto const t1 = tg.peek().value();
        CHECK(t1 >= 53);
        CHECK(t1 <= 55);
        seen_t1_53 = seen_t1_53 || t1 == 53;
        seen_t1_54 = seen_t1_54 || t1 == 54;
        seen_t1_55 = seen_t1_55 || t1 == 55;
        tg.pop();
    }
    CHECK(seen_t0_43);
    CHECK(seen_t0_44);
    CHECK(seen_t1_53);
    CHECK(seen_t1_54);
    CHECK(seen_t1_55);
}

TEST_CASE("dynamic dithered linear timeing generator",
          "[dynamic_dithered_linear_timing_generator]") {
    auto tg = dynamic_dithered_linear_timing_generator();
    CHECK_FALSE(tg.peek().has_value());
    struct trig_evt {
        i64 abstime;
        double delay;
        double interval;
        std::size_t count;
    };
    tg.trigger(trig_evt{42, 1.5, 10.25, 2});
    CHECK(tg.peek().has_value());
    auto const t0 = tg.peek().value();
    CHECK(t0 >= 43);
    CHECK(t0 <= 44);
    tg.pop();
    CHECK(tg.peek().has_value());
    auto const t1 = tg.peek().value();
    CHECK(t1 >= 53);
    CHECK(t1 <= 55);
    tg.pop();
    CHECK_FALSE(tg.peek().has_value());
}

// NOLINTEND(bugprone-unchecked-optional-access)

} // namespace tcspc
