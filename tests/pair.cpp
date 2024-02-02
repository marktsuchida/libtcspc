/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/pair.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_all.hpp>

#include <array>
#include <memory>

namespace tcspc {

namespace {

using out_events = event_set<detection_event<>, detection_pair_event<>>;

}

TEST_CASE("introspect pair", "[introspect]") {
    check_introspect_simple_processor(pair_all<1>(0, {1}, 1, null_sink()));
    check_introspect_simple_processor(pair_one<1>(0, {1}, 1, null_sink()));
    check_introspect_simple_processor(
        pair_all_between<1>(0, {1}, 1, null_sink()));
    check_introspect_simple_processor(
        pair_one_between<1>(0, {1}, 1, null_sink()));
}

TEST_CASE("pair all") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_all(0, std::array<default_data_traits::channel_type, 1>{1}, 2,
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair all with self") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_all(0, std::array<default_data_traits::channel_type, 1>{0}, 2,
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair one") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_one(0, std::array<default_data_traits::channel_type, 1>{1}, 2,
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair one with self") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_one(0, std::array<default_data_traits::channel_type, 1>{0}, 2,
                 capture_output<out_events>(
                     ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair all between") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(pair_all_between(
        0, std::array<default_data_traits::channel_type, 1>{1}, 2,
        capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair all between with self") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(pair_all_between(
        0, std::array<default_data_traits::channel_type, 1>{0}, 2,
        capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("pair one between") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(pair_one_between(
        0, std::array<default_data_traits::channel_type, 1>{1}, 2,
        capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("pair one between with self") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<detection_event<>>>(pair_one_between(
        0, std::array<default_data_traits::channel_type, 1>{0}, 2,
        capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
