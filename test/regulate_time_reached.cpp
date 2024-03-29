/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/regulate_time_reached.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

#include <cstddef>
#include <limits>
#include <memory>

namespace tcspc {

namespace {

using abstime_type = default_data_traits::abstime_type;
using other_event = timestamped_test_event<0>;
using events = event_set<other_event, time_reached_event<>>;

} // namespace

TEST_CASE("regulate time reached by abstime", "[regulate_time_reached]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<events>(regulate_time_reached(
        10, std::numeric_limits<std::size_t>::max(),
        capture_output<events>(ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream yields empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single time-reached event") {
        in.feed(time_reached_event<>{{42}});
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single other event") {
        in.feed(other_event{42});
        REQUIRE(out.check(other_event{42}));
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("exact time reached emitted") {
        in.feed(other_event{42});
        REQUIRE(out.check(other_event{42}));
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(other_event{43});
        REQUIRE(out.check(other_event{43}));
        in.flush();
        REQUIRE(out.check(time_reached_event<>{{43}}));
        REQUIRE(out.check_flushed());
    }

    SECTION("time reached emitted after threshold") {
        in.feed(other_event{42});
        REQUIRE(out.check(other_event{42}));
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(other_event{43});
        REQUIRE(out.check(other_event{43}));
        in.feed(other_event{51});
        REQUIRE(out.check(other_event{51}));
        in.feed(other_event{52});
        REQUIRE(out.check(other_event{52}));
        REQUIRE(out.check(time_reached_event<>{{52}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("excess time-reached events discarded") {
        in.feed(time_reached_event<>{{42}});
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(time_reached_event<>{{42}});
        in.feed(time_reached_event<>{{43}});
        in.feed(time_reached_event<>{{51}});
        in.feed(time_reached_event<>{{52}});
        REQUIRE(out.check(time_reached_event<>{{52}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("regulate time reached by count", "[regulate_time_reached]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<events>(regulate_time_reached(
        std::numeric_limits<abstime_type>::max(), 2,
        capture_output<events>(ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream yields empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single time-reached event") {
        in.feed(time_reached_event<>{{42}});
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("single other event") {
        in.feed(other_event{42});
        REQUIRE(out.check(other_event{42}));
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("exact time reached emitted") {
        in.feed(other_event{42});
        REQUIRE(out.check(other_event{42}));
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(other_event{43});
        REQUIRE(out.check(other_event{43}));
        in.flush();
        REQUIRE(out.check(time_reached_event<>{{43}}));
        REQUIRE(out.check_flushed());
    }

    SECTION("time reached emitted after threshold") {
        in.feed(other_event{42});
        REQUIRE(out.check(other_event{42}));
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(other_event{43});
        REQUIRE(out.check(other_event{43}));
        in.feed(other_event{44});
        REQUIRE(out.check(other_event{44}));
        REQUIRE(out.check(time_reached_event<>{{44}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("excess time-reached events discarded") {
        in.feed(time_reached_event<>{{42}});
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(time_reached_event<>{{42}});
        in.feed(time_reached_event<>{{43}});
        in.feed(time_reached_event<>{{51}});
        in.feed(time_reached_event<>{{52}});
        in.flush();
        REQUIRE(out.check(time_reached_event<>{{52}}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("regulate time reached, zero count threshold",
          "[regulate_time_reached]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<events>(regulate_time_reached(
        std::numeric_limits<abstime_type>::max(), 0,
        capture_output<events>(ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream yields empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("time reached emitted after every event") {
        in.feed(other_event{42});
        REQUIRE(out.check(other_event{42}));
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(other_event{43});
        REQUIRE(out.check(other_event{43}));
        REQUIRE(out.check(time_reached_event<>{{43}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("all time-reached events passed") {
        in.feed(time_reached_event<>{{42}});
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(time_reached_event<>{{42}});
        REQUIRE(out.check(time_reached_event<>{{42}}));
        in.feed(time_reached_event<>{{43}});
        REQUIRE(out.check(time_reached_event<>{{43}}));
        in.feed(time_reached_event<>{{52}});
        REQUIRE(out.check(time_reached_event<>{{52}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
