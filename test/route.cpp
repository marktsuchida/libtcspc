/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/route.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using tc_event = time_correlated_detection_event<>;
using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("Route", "[route]") {
    using out_events = event_set<tc_event, marker_event<>>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<tc_event, marker_event<>>>(
        route<event_set<tc_event>, event_set<marker_event<>>>(
            channel_router(std::array{
                std::pair{5, 0},
                std::pair{-3, 1},
                std::pair{-32768, 2},
            }),
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out0")),
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out1")),
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out2"))));
    in.require_output_checked(ctx, "out0");
    in.require_output_checked(ctx, "out1");
    in.require_output_checked(ctx, "out2");
    auto out0 = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out0"));
    auto out1 = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out1"));
    auto out2 = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out2"));

    SECTION("Route and broadcast by event type") {
        in.feed(tc_event{{{100}, 5}, 123});
        REQUIRE(out0.check(tc_event{{{100}, 5}, 123}));
        in.feed(tc_event{{{101}, -3}, 123});
        REQUIRE(out1.check(tc_event{{{101}, -3}, 123}));
        in.feed(tc_event{{{102}, 0}, 124});
        in.feed(marker_event<>{{{103}, 0}});
        REQUIRE(out0.check(marker_event<>{{{103}, 0}}));
        REQUIRE(out1.check(marker_event<>{{{103}, 0}}));
        REQUIRE(out2.check(marker_event<>{{{103}, 0}}));
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
        REQUIRE(out2.check_flushed());
    }

    SECTION("Error on routed propagates without flushing others") {
        out1.throw_error_on_next();
        REQUIRE_THROWS(in.feed(tc_event{{{101}, -3}, 123}));
        REQUIRE(out0.check_not_flushed());
        REQUIRE(out2.check_not_flushed());
    }

    SECTION("End on routed propagates, flushing others") {
        SECTION("Others not throwing") {
            out1.throw_end_processing_on_next();
            REQUIRE_THROWS_AS(in.feed(tc_event{{{101}, -3}, 123}),
                              end_processing);
            REQUIRE(out0.check_flushed());
            REQUIRE(out2.check_flushed());
        }

        SECTION("Other throwing error") {
            out1.throw_end_processing_on_next();
            out2.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.feed(tc_event{{{101}, -3}, 123}),
                              std::runtime_error);
            REQUIRE(out0.check_flushed());
        }

        SECTION("Other throwing end") {
            out1.throw_end_processing_on_next();
            out2.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.feed(tc_event{{{101}, -3}, 123}),
                              end_processing);
            REQUIRE(out0.check_flushed());
        }
    }
}

TEST_CASE("Route with heterogeneous downstreams", "[route]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0>>(route<event_set<e0>, event_set<>>(
        []([[maybe_unused]] e0 const &event) { return std::size_t(0); },
        capture_output<event_set<e0>>(
            ctx->tracker<capture_output_access>("out0")),
        null_sink()));
    in.require_output_checked(ctx, "out0");
    auto out0 = capture_output_checker<event_set<e0>>(
        ctx->accessor<capture_output_access>("out0"));

    in.feed(e0{});
    REQUIRE(out0.check(e0{}));
    in.flush();
    REQUIRE(out0.check_flushed());
}

TEST_CASE("Broadcast", "[broadcast]") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<e0>>(broadcast<event_set<e0>>(
        capture_output<event_set<e0>>(
            ctx->tracker<capture_output_access>("out0")),
        capture_output<event_set<e0>>(
            ctx->tracker<capture_output_access>("out1")),
        capture_output<event_set<e0>>(
            ctx->tracker<capture_output_access>("out2"))));
    in.require_output_checked(ctx, "out0");
    in.require_output_checked(ctx, "out1");
    in.require_output_checked(ctx, "out2");
    auto out0 = capture_output_checker<event_set<e0>>(
        ctx->accessor<capture_output_access>("out0"));
    auto out1 = capture_output_checker<event_set<e0>>(
        ctx->accessor<capture_output_access>("out1"));
    auto out2 = capture_output_checker<event_set<e0>>(
        ctx->accessor<capture_output_access>("out2"));

    SECTION("Empty stream") {
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
        REQUIRE(out2.check_flushed());
    }

    SECTION("Events are broadcast") {
        in.feed(e0{});
        REQUIRE(out0.check(e0{}));
        REQUIRE(out1.check(e0{}));
        REQUIRE(out2.check(e0{}));
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
        REQUIRE(out2.check_flushed());
    }

    SECTION("Error on output propagates without flushing others") {
        out1.throw_error_on_next();
        REQUIRE_THROWS(in.feed(e0{}));
        REQUIRE(out0.check(e0{})); // Received before out1 threw
        REQUIRE(out0.check_not_flushed());
        REQUIRE(out2.check_not_flushed());
    }

    SECTION("End on output propagates, flushing others") {
        SECTION("Others not throwing") {
            out1.throw_end_processing_on_next();
            REQUIRE_THROWS_AS(in.feed(e0{}), end_processing);
            REQUIRE(out0.check(e0{})); // Received before out1 threw
            REQUIRE(out0.check_flushed());
            REQUIRE(out2.check_flushed());
        }

        SECTION("Other throwing error") {
            out1.throw_end_processing_on_next();
            out2.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.feed(e0{}), std::runtime_error);
            REQUIRE(out0.check(e0{})); // Received before out1 threw
            REQUIRE(out0.check_flushed());
        }

        SECTION("Other throwing end") {
            out1.throw_end_processing_on_next();
            out2.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.feed(e0{}), end_processing);
            REQUIRE(out0.check(e0{})); // Received before out1 threw
            REQUIRE(out0.check_flushed());
            REQUIRE(out2.check_flushed());
        }
    }

    SECTION("Error on output flush propagates without flushing others") {
        out1.throw_error_on_flush();
        REQUIRE_THROWS(in.flush());
        REQUIRE(out0.check_flushed()); // Flushed before out1 threw
        REQUIRE(out2.check_not_flushed());
    }

    SECTION("End on output flush propagates, flushing others") {
        SECTION("Others not throwing") {
            out1.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.flush(), end_processing);
            REQUIRE(out0.check_flushed());
            REQUIRE(out2.check_flushed());
        }

        SECTION("Other throwing error") {
            out1.throw_end_processing_on_flush();
            out2.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.flush(), std::runtime_error);
            REQUIRE(out0.check_flushed());
        }

        SECTION("Other throwing end") {
            out1.throw_end_processing_on_flush();
            out2.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.flush(), end_processing);
            REQUIRE(out0.check_flushed());
        }
    }
}

} // namespace tcspc
