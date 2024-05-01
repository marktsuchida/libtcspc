/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/pair.hpp"

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

#include <array>
#include <memory>

namespace tcspc {

TEST_CASE("pair_all event type constraints") {
    using proc_type = decltype(pair_all(
        arg::start_channel{0}, std::array{1, 2}, arg::time_window<i64>{100},
        sink_events<std::array<detection_event<>, 2>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, detection_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("pair_one event type constraints") {
    using proc_type = decltype(pair_one(
        arg::start_channel{0}, std::array{1, 2}, arg::time_window<i64>{100},
        sink_events<std::array<detection_event<>, 2>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, detection_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("pair_all_between event type constraints") {
    using proc_type = decltype(pair_all_between(
        arg::start_channel{0}, std::array{1, 2}, arg::time_window<i64>{100},
        sink_events<std::array<detection_event<>, 2>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, detection_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("pair_one_between event type constraints") {
    using proc_type = decltype(pair_one_between(
        arg::start_channel{0}, std::array{1, 2}, arg::time_window<i64>{100},
        sink_events<std::array<detection_event<>, 2>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, detection_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("introspect pair", "[introspect]") {
    check_introspect_simple_processor(pair_all<1>(
        arg::start_channel{0}, {1}, arg::time_window<i64>{1}, null_sink()));
    check_introspect_simple_processor(pair_one<1>(
        arg::start_channel{0}, {1}, arg::time_window<i64>{1}, null_sink()));
    check_introspect_simple_processor(pair_all_between<1>(
        arg::start_channel{0}, {1}, arg::time_window<i64>{1}, null_sink()));
    check_introspect_simple_processor(pair_one_between<1>(
        arg::start_channel{0}, {1}, arg::time_window<i64>{1}, null_sink()));
}

namespace {

using out_events =
    type_list<detection_event<>, std::array<detection_event<>, 2>>;

} // namespace

TEST_CASE("pair all") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, pair_all(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{1},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {2, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {2, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair all with self") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, pair_all(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{0},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.handle(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.handle(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {2, 0}}}));
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.handle(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 0}}}));
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair one") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, pair_one(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{1},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair one with self") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, pair_one(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{0},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.handle(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.handle(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.handle(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair all between") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        pair_all_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{1},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair all between with self") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        pair_all_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{0},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.handle(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.handle(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.handle(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair one between") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        pair_one_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{1},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.handle(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.handle(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.handle(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.handle(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.handle(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.handle(detection_event<>{3, 1});
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.handle(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair one between with self") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        pair_one_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{0},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.handle(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.handle(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.handle(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
