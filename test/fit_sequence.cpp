/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/fit_sequence.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace tcspc {

namespace internal {

TEST_CASE("linear fit sequence", "[fit_arithmetic_time_sequence]") {
    using Catch::Matchers::WithinAbs;

    // Numbers derived from Wikipedia linear least squares example.
    std::vector const y{6.0, 5.0, 7.0, 10.0};
    auto const result = linear_fit_sequence(y);
    CHECK_THAT(result.intercept, WithinAbs(4.9, 1e-100));
    CHECK_THAT(result.slope, WithinAbs(1.4, 1e-100));
    CHECK_THAT(result.mse, WithinAbs(2.1, 1e-12));

    std::vector const yminlen{3.0, 4.0};
    auto const resultminlen = linear_fit_sequence(yminlen);
    CHECK_THAT(resultminlen.intercept, WithinAbs(3.0, 1e-100));
    CHECK_THAT(resultminlen.slope, WithinAbs(1.0, 1e-100));
    CHECK(std::isnan(resultminlen.mse));

    std::vector const ybad{3.0};
    auto const resultbad = linear_fit_sequence(ybad);
    CHECK(std::isnan(resultbad.intercept));
    CHECK(std::isnan(resultbad.slope));
    CHECK(std::isnan(resultbad.mse));

    std::vector<double> const yempty{};
    auto const resultempty = linear_fit_sequence(ybad);
    CHECK(std::isnan(resultempty.intercept));
    CHECK(std::isnan(resultempty.slope));
    CHECK(std::isnan(resultempty.mse));
}

} // namespace internal

TEST_CASE("fit arithmetic time sequence", "[fit_arithmetic_time_sequence]") {
    using e0 = timestamped_test_event<0>;
    auto out = capture_output<event_set<start_and_interval_event<>>>();
    auto in = feed_input<event_set<e0>>(
        fit_arithmetic_time_sequence<default_data_traits, e0>(
            4, {1, 2}, 2.5, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("fit succeeds") {
        in.feed(e0{6});
        in.feed(e0{5});
        in.feed(e0{7});
        in.feed(e0{10});
        auto const out_event = out.retrieve<start_and_interval_event<>>();
        REQUIRE(out_event.has_value());
        REQUIRE(out_event->abstime == 5);
        REQUIRE_THAT(out_event->interval,
                     Catch::Matchers::WithinRel(1.4, 1e-6));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("fit fails") {
        in.feed(e0{100});
        in.feed(e0{5});
        in.feed(e0{7});
        in.feed(e0{10});
        REQUIRE_THROWS(out.check_end());
    }
}

TEST_CASE("fit arithmetic time sequence time bound, signed abstime",
          "[fit_arithmetic_time_sequence]") {
    using abstime_type = default_data_traits::abstime_type;
    using e0 = timestamped_test_event<0>;
    static_assert(std::is_signed_v<abstime_type>);
    auto out = capture_output<event_set<start_and_interval_event<>>>();
    auto in = feed_input<event_set<e0>>(
        fit_arithmetic_time_sequence<default_data_traits, e0>(
            1000, {99, 101}, std::numeric_limits<double>::infinity(),
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("fail with time bound error") {
        // Constrain the fit well with lots of points, but have the last point
        // have a large positive error; counteract that by a negative error on
        // the penultimate point to keep the estimated interval relatively
        // unaffected.
        for (int i = 0; i < 998; ++i)
            in.feed(e0{i * abstime_type(100)});
        static constexpr abstime_type offset = 2000;
        in.feed(e0{99800 - offset});
        in.feed(e0{99900 + offset});
        REQUIRE_THROWS_WITH(out.check_end(),
                            Catch::Matchers::ContainsSubstring("time bound"));
    }

    SECTION("correctly handle negative estimated start time") {
        in.feed(e0{0});
        in.feed(e0{0});
        for (int i = 0; i < 998; ++i)
            in.feed(e0{i * abstime_type(100)});
        auto const out_event = out.retrieve<start_and_interval_event<>>();
        REQUIRE(out_event.has_value());
        REQUIRE(out_event->abstime == -199);
        REQUIRE_THAT(out_event->interval,
                     Catch::Matchers::WithinRel(99.9982, 1e-6));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("fit arithmetic time sequence time bound, unsigned abstime",
          "[fit_arithmetic_time_sequence]") {
    struct traits {
        using abstime_type = std::uint64_t;
    };
    using abstime_type = traits::abstime_type;
    using e0 = timestamped_test_event<0, traits>;
    auto out = capture_output<event_set<start_and_interval_event<traits>>>();
    auto in =
        feed_input<event_set<e0>>(fit_arithmetic_time_sequence<traits, e0>(
            1000, {99, 101}, std::numeric_limits<double>::infinity(),
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("succeed despite time bound would be negative") {
        for (abstime_type i = 0; i < 1000; ++i)
            in.feed(e0{i * abstime_type(100)});
        // Time bound criterion would be that estimated start time is
        // 99900 - 1000 * 100 = -100, but we handle this despite using an
        // unsigned abstime
        auto const out_event =
            out.retrieve<start_and_interval_event<traits>>();
        REQUIRE(out_event.has_value());
        REQUIRE(out_event->abstime == 0);
        REQUIRE_THAT(out_event->interval,
                     Catch::Matchers::WithinRel(100.0, 1e-6));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("fail with time bound error") {
        // Same as signed version
        for (abstime_type i = 0; i < 998; ++i)
            in.feed(e0{i * abstime_type(100)});
        static constexpr abstime_type offset = 2000;
        in.feed(e0{99800 - offset});
        in.feed(e0{99900 + offset});
        REQUIRE_THROWS_WITH(out.check_end(),
                            Catch::Matchers::ContainsSubstring("time bound"));
    }

    SECTION("fail because of negative estimated start time") {
        in.feed(e0{0});
        in.feed(e0{0});
        for (abstime_type i = 0; i < 998; ++i)
            in.feed(e0{i * abstime_type(100)});
        REQUIRE_THROWS_WITH(out.check_end(),
                            Catch::Matchers::ContainsSubstring("unsigned"));
    }
}

} // namespace tcspc
