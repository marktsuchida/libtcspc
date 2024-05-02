/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/regulate_time_reached.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <limits>
#include <memory>

namespace tcspc {

namespace {

using abstime_type = default_data_types::abstime_type;
using other_event = time_tagged_test_event<0>;
using events = type_list<other_event, time_reached_event<>>;

} // namespace

TEST_CASE("type constraints: regulate_time_reached") {
    using proc_type = decltype(regulate_time_reached(
        arg::interval_threshold<i64>{1}, arg::count_threshold<std::size_t>{1},
        sink_events<time_reached_event<>, other_event>()));
    STATIC_CHECK(is_processor_v<proc_type, time_reached_event<>, other_event>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("introspect: regulate_time_reached") {
    check_introspect_simple_processor(regulate_time_reached(
        arg::interval_threshold<i64>{1}, arg::count_threshold<std::size_t>{1},
        null_sink()));
}

TEST_CASE("regulate time reached by abstime") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        regulate_time_reached(
            arg::interval_threshold<i64>{10},
            arg::count_threshold{std::numeric_limits<std::size_t>::max()},
            capture_output<events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<events>(valcat, ctx, "out");

    SECTION("empty stream yields empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single time-reached event") {
        in.handle(time_reached_event<>{42});
        REQUIRE(out.check(time_reached_event<>{42}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single other event") {
        in.handle(other_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("exact time reached emitted") {
        in.handle(other_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(other_event{43});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{43}));
        in.flush();
        REQUIRE(out.check(time_reached_event<>{43}));
        REQUIRE(out.check_flushed());
    }

    SECTION("time reached emitted after threshold") {
        in.handle(other_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(other_event{43});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{43}));
        in.handle(other_event{51});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{51}));
        in.handle(other_event{52});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{52}));
        REQUIRE(out.check(time_reached_event<>{52}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("excess time-reached events discarded") {
        in.handle(time_reached_event<>{42});
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(time_reached_event<>{42});
        in.handle(time_reached_event<>{43});
        in.handle(time_reached_event<>{51});
        in.handle(time_reached_event<>{52});
        REQUIRE(out.check(time_reached_event<>{52}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("regulate time reached by count") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        regulate_time_reached(
            arg::interval_threshold{std::numeric_limits<abstime_type>::max()},
            arg::count_threshold<std::size_t>{2},
            capture_output<events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<events>(valcat, ctx, "out");

    SECTION("empty stream yields empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single time-reached event") {
        in.handle(time_reached_event<>{42});
        REQUIRE(out.check(time_reached_event<>{42}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single other event") {
        in.handle(other_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("exact time reached emitted") {
        in.handle(other_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(other_event{43});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{43}));
        in.flush();
        REQUIRE(out.check(time_reached_event<>{43}));
        REQUIRE(out.check_flushed());
    }

    SECTION("time reached emitted after threshold") {
        in.handle(other_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(other_event{43});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{43}));
        in.handle(other_event{44});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{44}));
        REQUIRE(out.check(time_reached_event<>{44}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("excess time-reached events discarded") {
        in.handle(time_reached_event<>{42});
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(time_reached_event<>{42});
        in.handle(time_reached_event<>{43});
        in.handle(time_reached_event<>{51});
        in.handle(time_reached_event<>{52});
        in.flush();
        REQUIRE(out.check(time_reached_event<>{52}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("regulate time reached, zero count threshold",
          "[regulate_time_reached]") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        regulate_time_reached(
            arg::interval_threshold{std::numeric_limits<abstime_type>::max()},
            arg::count_threshold<std::size_t>{0},
            capture_output<events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<events>(valcat, ctx, "out");

    SECTION("empty stream yields empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("time reached emitted after every event") {
        in.handle(other_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{42}));
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(other_event{43});
        REQUIRE(out.check(emitted_as::same_as_fed, other_event{43}));
        REQUIRE(out.check(time_reached_event<>{43}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("all time-reached events passed") {
        in.handle(time_reached_event<>{42});
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(time_reached_event<>{42});
        REQUIRE(out.check(time_reached_event<>{42}));
        in.handle(time_reached_event<>{43});
        REQUIRE(out.check(time_reached_event<>{43}));
        in.handle(time_reached_event<>{52});
        REQUIRE(out.check(time_reached_event<>{52}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
