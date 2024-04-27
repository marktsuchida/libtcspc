/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/pair.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>

namespace tcspc {

namespace {

using out_events =
    type_list<detection_event<>, std::array<detection_event<>, 2>>;

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

TEST_CASE("pair all") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_all(arg::start_channel{0},
                 std::array<default_data_types::channel_type, 1>{1},
                 arg::time_window<i64>{2},
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {2, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {2, 1}}}));
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair all with self") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_all(arg::start_channel{0},
                 std::array<default_data_types::channel_type, 1>{0},
                 arg::time_window<i64>{2},
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.feed(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.feed(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {2, 0}}}));
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.feed(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 0}}}));
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair one") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_one(arg::start_channel{0},
                 std::array<default_data_types::channel_type, 1>{1},
                 arg::time_window<i64>{2},
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair one with self") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_one(arg::start_channel{0},
                 std::array<default_data_types::channel_type, 1>{0},
                 arg::time_window<i64>{2},
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.feed(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.feed(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.feed(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair all between") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_all_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{1},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair all between with self") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_all_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{0},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.feed(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.feed(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.feed(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair one between") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_one_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{1},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {0, 1}}}));
        REQUIRE(out.check(detection_event<>{0, 1}));

        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));
        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));
        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{3, 0}, {3, 1}}}));
        REQUIRE(out.check(detection_event<>{3, 1}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{0, 1});
        REQUIRE(out.check(detection_event<>{0, 1}));
        in.feed(detection_event<>{0, 0});
        REQUIRE(out.check(detection_event<>{0, 0}));

        in.feed(detection_event<>{1, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 1}}}));
        REQUIRE(out.check(detection_event<>{1, 1}));
        in.feed(detection_event<>{1, 0});
        REQUIRE(out.check(detection_event<>{1, 0}));

        in.feed(detection_event<>{2, 1});
        REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 1}}}));
        REQUIRE(out.check(detection_event<>{2, 1}));

        in.feed(detection_event<>{3, 1});
        REQUIRE(out.check(detection_event<>{3, 1}));
        in.feed(detection_event<>{3, 0});
        REQUIRE(out.check(detection_event<>{3, 0}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair one between with self") {
    auto ctx = context::create();
    auto in = feed_input<type_list<detection_event<>>>(
        pair_one_between(arg::start_channel{0},
                         std::array<default_data_types::channel_type, 1>{0},
                         arg::time_window<i64>{2},
                         capture_output<out_events>(
                             ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(detection_event<>{0, 0});
    REQUIRE(out.check(detection_event<>{0, 0}));

    in.feed(detection_event<>{1, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{0, 0}, {1, 0}}}));
    REQUIRE(out.check(detection_event<>{1, 0}));

    in.feed(detection_event<>{2, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{1, 0}, {2, 0}}}));
    REQUIRE(out.check(detection_event<>{2, 0}));

    in.feed(detection_event<>{3, 0});
    REQUIRE(out.check(std::array<detection_event<>, 2>{{{2, 0}, {3, 0}}}));
    REQUIRE(out.check(detection_event<>{3, 0}));

    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
