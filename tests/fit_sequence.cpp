/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/fit_sequence.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/timing_misc.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace tcspc {

TEST_CASE("fit_periodic_sequences event type constraints") {
    struct tick {
        i64 abstime;
    };
    using proc_type = decltype(fit_periodic_sequences<tick>(
        arg::length<std::size_t>{42}, arg::min_interval{10.0},
        arg::max_interval{20.0}, arg::max_mse{1.0},
        sink_events<periodic_sequence_model_event<>, int>()));

    STATIC_CHECK(is_processor_v<proc_type, tick, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("introspect fit_sequence") {
    check_introspect_simple_processor(fit_periodic_sequences<int>(
        arg::length<std::size_t>{3}, arg::min_interval{0.0},
        arg::max_interval{1.0}, arg::max_mse{0.5}, null_sink()));
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
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, fit_periodic_sequences<e0>(
                    arg::length<std::size_t>{4}, arg::min_interval{1.0},
                    arg::max_interval{2.0}, arg::max_mse{2.5},
                    capture_output<type_list<periodic_sequence_model_event<>>>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<periodic_sequence_model_event<>>>(
            valcat, ctx, "out");

    SECTION("fit succeeds") {
        in.handle(e0{6});
        in.handle(e0{5});
        in.handle(e0{7});
        in.handle(e0{10});
        auto const out_event = out.pop<periodic_sequence_model_event<>>();
        CHECK(out_event.abstime == 10);
        CHECK_THAT(static_cast<double>(out_event.abstime) + out_event.delay,
                   Catch::Matchers::WithinRel(4.9, 1e-12));
        CHECK_THAT(out_event.interval, Catch::Matchers::WithinRel(1.4, 1e-12));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("fit fails") {
        in.handle(e0{100});
        in.handle(e0{5});
        in.handle(e0{7});
        REQUIRE_THROWS_AS(in.handle(e0{10}), model_fit_error);
        REQUIRE(out.check_not_flushed());
    }
}

} // namespace tcspc
