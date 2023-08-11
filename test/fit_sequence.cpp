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

TEST_CASE("periodic fitter", "[fit_periodic_sequences]") {
    using Catch::Matchers::WithinAbs;

    // Numbers derived from Wikipedia linear least squares example.
    std::vector const y{6.0, 5.0, 7.0, 10.0};
    auto const result = periodic_fitter(y.size()).fit(y);
    CHECK_THAT(result.intercept, WithinAbs(4.9, 1e-100));
    CHECK_THAT(result.slope, WithinAbs(1.4, 1e-100));
    CHECK_THAT(result.mse, WithinAbs(2.1, 1e-12));

    std::vector const yminlen{3.0, 4.0};
    auto const resultminlen = periodic_fitter(2).fit(yminlen);
    CHECK_THAT(resultminlen.intercept, WithinAbs(3.0, 1e-100));
    CHECK_THAT(resultminlen.slope, WithinAbs(1.0, 1e-100));
    CHECK(std::isnan(resultminlen.mse));

    std::vector const ybad{3.0};
    auto const resultbad = periodic_fitter(1).fit(ybad);
    CHECK(std::isnan(resultbad.intercept));
    CHECK(std::isnan(resultbad.slope));

    std::vector<double> const yempty{};
    auto const resultempty = periodic_fitter(0).fit(yempty);
    CHECK(std::isnan(resultempty.intercept));
    CHECK(std::isnan(resultempty.slope));
}

} // namespace internal

TEST_CASE("fit periodic sequences", "[fit_periodic_sequences]") {
    using e0 = timestamped_test_event<0>;
    auto out = capture_output<event_set<offset_and_interval_event<>>>();
    auto in = feed_input<event_set<e0>>(
        fit_periodic_sequences<default_data_traits, e0>(4, {1.0, 2.0}, 2.5,
                                                        ref_processor(out)));
    in.require_output_checked(out);

    SECTION("fit succeeds") {
        in.feed(e0{6});
        in.feed(e0{5});
        in.feed(e0{7});
        in.feed(e0{10});
        auto const out_event = out.retrieve<offset_and_interval_event<>>();
        REQUIRE(out_event.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(out_event->offset >= 1.0);
        REQUIRE(out_event->offset < 2.0);
        REQUIRE_THAT(static_cast<double>(out_event->abstime) +
                         out_event->offset,
                     Catch::Matchers::WithinRel(4.9, 1e-12));
        REQUIRE_THAT(out_event->interval,
                     Catch::Matchers::WithinRel(1.4, 1e-12));
        // NOLINTEND(bugprone-unchecked-optional-access)
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("fit fails") {
        in.feed(e0{100});
        in.feed(e0{5});
        in.feed(e0{7});
        REQUIRE_THROWS(in.feed(e0{10}));
        REQUIRE(out.check_not_flushed());
    }
}

TEST_CASE("fit periodic sequences time bound, signed abstime",
          "[fit_periodic_sequences]") {
    using abstime_type = default_data_traits::abstime_type;
    using e0 = timestamped_test_event<0>;
    static_assert(std::is_signed_v<abstime_type>);
    auto out = capture_output<event_set<offset_and_interval_event<>>>();
    auto in = feed_input<event_set<e0>>(
        fit_periodic_sequences<default_data_traits, e0>(
            1000, {99.0, 101.0}, std::numeric_limits<double>::infinity(),
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
        REQUIRE_THROWS_WITH(in.feed(e0{99900 + offset}),
                            Catch::Matchers::ContainsSubstring("time bound"));
        REQUIRE(out.check_not_flushed());
    }

    SECTION("correctly handle negative estimated start time") {
        in.feed(e0{0});
        in.feed(e0{0});
        for (int i = 0; i < 998; ++i)
            in.feed(e0{i * abstime_type(100)});
        auto const out_event = out.retrieve<offset_and_interval_event<>>();
        REQUIRE(out_event.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(out_event->offset >= 1.0);
        REQUIRE(out_event->offset < 2.0);
        REQUIRE_THAT(static_cast<double>(out_event->abstime) +
                         out_event->offset,
                     Catch::Matchers::WithinAbs(-198.8, 0.1));
        REQUIRE_THAT(out_event->interval,
                     Catch::Matchers::WithinRel(99.9982, 1e-6));
        // NOLINTEND(bugprone-unchecked-optional-access)
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("fit periodic sequences time bound, unsigned abstime",
          "[fit_periodic_sequences]") {
    struct traits {
        using abstime_type = std::uint64_t;
    };
    using abstime_type = traits::abstime_type;
    using e0 = timestamped_test_event<0, traits>;
    auto out = capture_output<event_set<offset_and_interval_event<traits>>>();
    auto in = feed_input<event_set<e0>>(fit_periodic_sequences<traits, e0>(
        1000, {99.0, 101.0}, std::numeric_limits<double>::infinity(),
        ref_processor(out)));
    in.require_output_checked(out);

    SECTION("succeed despite time bound would be negative") {
        for (abstime_type i = 0; i < 1000; ++i)
            in.feed(e0{1 + i * abstime_type(100)});
        // Time bound criterion would be that estimated start time is
        // 99900 - 1000 * 100 = -100, but we handle this despite using an
        // unsigned abstime
        auto const out_event =
            out.retrieve<offset_and_interval_event<traits>>();
        REQUIRE(out_event.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        REQUIRE(out_event->offset >= 1.0);
        REQUIRE(out_event->offset < 2.0);
        REQUIRE_THAT(static_cast<double>(out_event->abstime) +
                         out_event->offset,
                     Catch::Matchers::WithinRel(1.0, 1e-12));
        REQUIRE_THAT(out_event->interval,
                     Catch::Matchers::WithinRel(100.0, 1e-6));
        // NOLINTEND(bugprone-unchecked-optional-access)
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION(
        "fail because of negative estimated start time (positive intercept less than tick offset)") {
        in.feed(e0{50});
        for (abstime_type i = 0; i < 998; ++i)
            in.feed(e0{90 + i * abstime_type(100)});
        REQUIRE_THROWS_WITH(in.feed(e0{90 + 998 * abstime_type(100)}),
                            Catch::Matchers::ContainsSubstring("unsigned"));
        REQUIRE(out.check_not_flushed());
    }

    SECTION(
        "fail because of negative estimated start time (negative intercept)") {
        in.feed(e0{0});
        in.feed(e0{0});
        for (abstime_type i = 0; i < 997; ++i)
            in.feed(e0{i * abstime_type(100)});
        REQUIRE_THROWS_WITH(in.feed(e0{997 * abstime_type(100)}),
                            Catch::Matchers::ContainsSubstring("unsigned"));
        REQUIRE(out.check_not_flushed());
    }
}

} // namespace tcspc
