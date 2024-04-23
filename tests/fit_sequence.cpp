/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/fit_sequence.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/timing_misc.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace tcspc {

TEST_CASE("introspect fit_sequence") {
    check_introspect_simple_processor(
        fit_periodic_sequences<int>(3, {0.0, 1.0}, 0.5, null_sink()));
}

namespace internal {

TEST_CASE("periodic fitter") {
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

TEST_CASE("fit periodic sequences") {
    using e0 = time_tagged_test_event<0>;
    auto ctx = processor_context::create();
    auto in = feed_input<type_list<e0>>(fit_periodic_sequences<e0>(
        4, {1.0, 2.0}, 2.5,
        capture_output<type_list<periodic_sequence_event<>>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<periodic_sequence_event<>>>(
        ctx->access<capture_output_access>("out"));

    SECTION("fit succeeds") {
        in.feed(e0{6});
        in.feed(e0{5});
        in.feed(e0{7});
        in.feed(e0{10});
        auto const out_event = out.pop<periodic_sequence_event<>>();
        CHECK(out_event.abstime == 10);
        CHECK_THAT(static_cast<double>(out_event.abstime) + out_event.delay,
                   Catch::Matchers::WithinRel(4.9, 1e-12));
        CHECK_THAT(out_event.interval, Catch::Matchers::WithinRel(1.4, 1e-12));
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

} // namespace tcspc
