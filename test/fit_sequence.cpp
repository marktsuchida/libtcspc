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
    auto out = capture_output<event_set<periodic_sequence_event<>>>();
    auto in = feed_input<event_set<e0>>(
        fit_periodic_sequences<default_data_traits, e0>(4, {1.0, 2.0}, 2.5,
                                                        ref_processor(out)));
    in.require_output_checked(out);

    SECTION("fit succeeds") {
        in.feed(e0{6});
        in.feed(e0{5});
        in.feed(e0{7});
        in.feed(e0{10});
        auto const out_event = out.retrieve<periodic_sequence_event<>>();
        REQUIRE(out_event.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        CHECK(out_event->abstime == 10);
        CHECK_THAT(static_cast<double>(out_event->abstime) +
                       out_event->start_offset,
                   Catch::Matchers::WithinRel(4.9, 1e-12));
        CHECK_THAT(out_event->interval,
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

TEST_CASE("retime periodic sequence events",
          "[retime_periodic_sequence_events]") {
    auto out = capture_output<event_set<periodic_sequence_event<>>>();
    auto in = feed_input<event_set<periodic_sequence_event<>>>(
        retime_periodic_sequence_events<>(10, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("normal operation") {
        in.feed(periodic_sequence_event<>{4, -8.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{-5, 1.0, 1.5}));
        in.feed(periodic_sequence_event<>{4, -8.5, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{-6, 1.5, 1.5}));
        in.feed(periodic_sequence_event<>{4, 10.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{13, 1.0, 1.5}));
    }

    SECTION("max time shift") {
        in.feed(periodic_sequence_event<>{4, -9.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{-6, 1.0, 1.5}));
        in.feed(periodic_sequence_event<>{4, 11.75, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{14, 1.75, 1.5}));
    }

    SECTION("fail above max time shift") {
        REQUIRE_THROWS_WITH(in.feed(periodic_sequence_event<>{4, -9.01, 1.5}),
                            Catch::Matchers::ContainsSubstring("shift"));
        REQUIRE_THROWS_WITH(in.feed(periodic_sequence_event<>{4, 12.0, 1.5}),
                            Catch::Matchers::ContainsSubstring("shift"));
    }
}

TEST_CASE("retime periodic sequence events unsigned",
          "[retime_periodic_sequence_events]") {
    struct traits {
        using abstime_type = std::uint64_t;
    };
    auto out = capture_output<event_set<periodic_sequence_event<traits>>>();
    auto in = feed_input<event_set<periodic_sequence_event<traits>>>(
        retime_periodic_sequence_events<traits>(10, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("normal operation") {
        in.feed(periodic_sequence_event<traits>{4, -1.5, 1.5});
        REQUIRE(out.check(periodic_sequence_event<traits>{1, 1.5, 1.5}));
        in.feed(periodic_sequence_event<traits>{4, -3.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<traits>{0, 1.0, 1.5}));
    }

    SECTION("unsigned underflow") {
        REQUIRE_THROWS_WITH(
            in.feed(periodic_sequence_event<traits>{4, -3.01, 1.5}),
            Catch::Matchers::ContainsSubstring("unsigned"));
    }
}

} // namespace tcspc
