/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/dither.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstddef>
#include <cstdint>

namespace tcspc {

namespace {

using trigger_event = timestamped_test_event<0>;
using output_event = timestamped_test_event<1>;

} // namespace

namespace internal {

TEST_CASE("uniformly distributed double") {
    CHECK(uniform_double_0_1(0) == 0.0);
    CHECK(uniform_double_0_1(std::uint64_t(-1)) < 1.0);
    CHECK_THAT(uniform_double_0_1(std::uint64_t(-1)),
               Catch::Matchers::WithinAbs(1.0, 1e-15));
}

TEST_CASE("uniformly distributed double, for std::minstd_rand") {
    CHECK(uniform_double_0_1_minstd(0) == 0.0);
    CHECK(uniform_double_0_1_minstd(1'073'741'824) == 0.5);
    CHECK(uniform_double_0_1_minstd(2'147'483'647) < 1.0);
    CHECK_THAT(uniform_double_0_1_minstd(2'147'483'647),
               Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("dither given random double in 1.0 to 2.0") {
    CHECK(dither<std::int64_t>(0.0, 0.0) == 0);
    CHECK(dither<std::int64_t>(0.1, 0.0) == 0);
    CHECK(dither<std::int64_t>(0.999, 0.0) == 0);
    CHECK(dither<std::int64_t>(1.0, 0.0) == 1);

    CHECK(dither<std::int64_t>(0.0, 0.9999) == 0);
    CHECK(dither<std::int64_t>(0.1, 0.9999) == 1);
    CHECK(dither<std::int64_t>(0.999, 0.9999) == 1);
    CHECK(dither<std::int64_t>(1.0, 0.9999) == 1);
}

TEST_CASE("dithering quantizer") {
    dithering_quantizer<std::int64_t> dq;
    CHECK(dq(-1.0) == -1);
    CHECK(dq(0.0) == 0);
    CHECK(dq(1.0) == 1);
    CHECK(dq(2.0) == 2);
    auto q = dq(1.5);
    CHECK(q >= 1);
    CHECK(q <= 2);
}

} // namespace internal

TEST_CASE("dithered one-shot timing generator",
          "[dithered_one_shot_timing_generator]") {
    auto tg = dithered_one_shot_timing_generator<output_event>(0.5);
    CHECK_FALSE(tg.peek().has_value());
    tg.trigger(trigger_event{42});
    CHECK(tg.peek().has_value());
    auto const t0 =
        tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
    auto const e0 = tg.pop();
    CHECK(t0 == e0.abstime);
    CHECK(t0 >= 42);
    CHECK(t0 <= 43);
    CHECK_FALSE(tg.peek().has_value());
}

TEST_CASE("dynamic dithered one-shot timing generator",
          "[dynamic_dithered_one_shot_timing_generator]") {
    auto tg = dynamic_dithered_one_shot_timing_generator<output_event>();
    CHECK_FALSE(tg.peek().has_value());
    struct trig_evt {
        std::int64_t abstime;
        double delay;
    };
    tg.trigger(trig_evt{42, 0.5});
    CHECK(tg.peek().has_value());
    auto const t0 =
        tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
    auto const e0 = tg.pop();
    CHECK(t0 == e0.abstime);
    CHECK(t0 >= 42);
    CHECK(t0 <= 43);
    CHECK_FALSE(tg.peek().has_value());
}

TEST_CASE("dithered linear timing generator",
          "[dithered_linear_timing_generator]") {
    // Test that generated timings always fulfill criteria, but also that all
    // possible values are seen within a reasonable number of repetitions.
    // (Note that this test should be reproducible because of our use of a
    // standardized PRNG with the same seed.)
    bool seen_t0_42 = false;
    bool seen_t0_43 = false;
    bool seen_t1_52 = false;
    bool seen_t1_53 = false;
    bool seen_t01_10 = false;
    bool seen_t01_11 = false;
    auto tg = dithered_linear_timing_generator<output_event>(0.5, 10.25, 2);
    for (std::size_t i = 0; i < 5; ++i) { // 5 is the sharp minimum to pass.
        CHECK_FALSE(tg.peek().has_value());
        tg.trigger(trigger_event{42});
        CHECK(tg.peek().has_value());
        auto const t0 =
            tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
        auto const e0 = tg.pop();
        CHECK(t0 == e0.abstime);
        CHECK(t0 >= 42);
        CHECK(t0 <= 43);
        seen_t0_42 = seen_t0_42 || t0 == 42;
        seen_t0_43 = seen_t0_43 || t0 == 43;
        CHECK(tg.peek().has_value());
        auto const t1 =
            tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
        auto const e1 = tg.pop();
        CHECK(t1 == e1.abstime);
        CHECK(t1 >= 52);
        CHECK(t1 <= 53);
        seen_t1_52 = seen_t1_52 || t1 == 52;
        seen_t1_53 = seen_t1_53 || t1 == 53;
        CHECK(t1 - t0 >= 10);
        CHECK(t1 - t0 <= 11);
        seen_t01_10 = seen_t01_10 || t1 - t0 == 10;
        seen_t01_11 = seen_t01_11 || t1 - t0 == 11;
    }
    CHECK(seen_t0_42);
    CHECK(seen_t0_43);
    CHECK(seen_t1_52);
    CHECK(seen_t1_53);
    CHECK(seen_t01_10);
    CHECK(seen_t01_11);
}

TEST_CASE("dynamic dithered linear timeing generator",
          "[dynamic_dithered_linear_timing_generator]") {
    auto tg = dynamic_dithered_linear_timing_generator<output_event>();
    CHECK_FALSE(tg.peek().has_value());
    struct trig_evt {
        std::int64_t abstime;
        double delay;
        double interval;
        std::size_t count;
    };
    tg.trigger(trig_evt{42, 0.5, 10.25, 2});
    CHECK(tg.peek().has_value());
    auto const t0 =
        tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
    auto const e0 = tg.pop();
    CHECK(t0 == e0.abstime);
    CHECK(t0 >= 42);
    CHECK(t0 <= 43);
    CHECK(tg.peek().has_value());
    auto const t1 =
        tg.peek().value(); // NOLINT(bugprone-unchecked-optional-access)
    auto const e1 = tg.pop();
    CHECK(t1 == e1.abstime);
    CHECK(t1 >= 52);
    CHECK(t1 <= 53);
    CHECK(t1 - t0 >= 10);
    CHECK(t1 - t0 <= 11);
}

} // namespace tcspc
