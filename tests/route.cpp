/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/route.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_erased_processor.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace {

using tc_event = time_correlated_detection_event<>;
using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("route_homogeneous constructed two ways have same type") {
    STATIC_CHECK(std::is_same_v<decltype(route_homogeneous<type_list<>>(
                                    null_router(), null_sink(), null_sink())),
                                decltype(route_homogeneous<type_list<>>(
                                    null_router(),
                                    std::array{null_sink(), null_sink()}))>);
    // So we need not further test the std::array case.
}

TEST_CASE("route and broadcast construct correct type") {
    STATIC_CHECK(std::is_same_v<
                 decltype(broadcast_homogeneous(null_sink(), null_sink())),
                 decltype(route_homogeneous<type_list<>>(
                     null_router(), null_sink(), null_sink()))>);

    STATIC_CHECK(std::is_same_v<
                 decltype(route<type_list<e0>, type_list<e1>>(
                     null_router(), null_sink(), null_sink())),
                 decltype(route_homogeneous<type_list<e0>>(
                     null_router(),
                     type_erased_processor<type_list<e0, e1>>(null_sink()),
                     type_erased_processor<type_list<e0, e1>>(null_sink())))>);

    STATIC_CHECK(std::is_same_v<
                 decltype(broadcast<type_list<e0>>(null_sink(), null_sink())),
                 decltype(route_homogeneous<type_list<>>(
                     null_router(),
                     type_erased_processor<type_list<e0>>(null_sink()),
                     type_erased_processor<type_list<e0>>(null_sink())))>);
    // So we only need to further test route_homogeneous.
}

TEST_CASE("route_homogeneous event types") {
    SECTION("handles flush") {
        STATIC_CHECK(handles_flush_v<decltype(route_homogeneous<type_list<>>(
                         null_router(), sink_events<type_list<>>()))>);
    }

    // We cannot distinguish between routed and broadcast events in SFINAE
    // context.
    SECTION("handles any event handled by downstream") {
        STATIC_CHECK(
            handles_event_v<decltype(route_homogeneous<type_list<>>(
                                null_router(), sink_events<type_list<e0>>())),
                            e0>);
        STATIC_CHECK(
            handles_event_v<decltype(route_homogeneous<type_list<e0>>(
                                null_router(), sink_events<type_list<e0>>())),
                            e0>);
    }
}

TEST_CASE("introspect route", "[introspect]") {
    auto const rh2 = route_homogeneous<type_list<>>(null_router(), null_sink(),
                                                    null_sink());
    auto const info = check_introspect_node_info(rh2);
    auto const g = rh2.introspect_graph();
    CHECK(g.nodes().size() == 3);
    CHECK(g.entry_points().size() == 1);
    auto const node = g.entry_points()[0];
    CHECK(g.node_info(node) == info);
    auto const edges = g.edges();
    CHECK(edges.size() == 2);
    CHECK(edges[0].first == node);
    CHECK(edges[1].first == node);
    CHECK(g.node_info(edges[0].second).name() == "null_sink");
    CHECK(g.node_info(edges[1].second).name() == "null_sink");
}

TEST_CASE("Route") {
    using out_events = type_list<tc_event, marker_event<>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, route<type_list<tc_event>, type_list<marker_event<>>>(
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
    auto out0 = capture_output_checker<out_events>(valcat, ctx, "out0");
    auto out1 = capture_output_checker<out_events>(valcat, ctx, "out1");
    auto out2 = capture_output_checker<out_events>(valcat, ctx, "out2");

    SECTION("Route and broadcast by event type") {
        in.handle(tc_event{100, 5, 123});
        REQUIRE(out0.check(emitted_as::same_as_fed, tc_event{100, 5, 123}));
        in.handle(tc_event{101, -3, 123});
        REQUIRE(out1.check(emitted_as::same_as_fed, tc_event{101, -3, 123}));
        in.handle(tc_event{102, 0, 124});
        in.handle(marker_event<>{103, 0});
        REQUIRE(out0.check(emitted_as::any_allowed, marker_event<>{103, 0}));
        REQUIRE(out1.check(emitted_as::any_allowed, marker_event<>{103, 0}));
        REQUIRE(out2.check(emitted_as::any_allowed, marker_event<>{103, 0}));
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
        REQUIRE(out2.check_flushed());
    }

    SECTION("Error on routed propagates without flushing others") {
        out1.throw_error_on_next();
        REQUIRE_THROWS(in.handle(tc_event{101, -3, 123}));
        REQUIRE(out0.check_not_flushed());
        REQUIRE(out2.check_not_flushed());
    }

    SECTION("End on routed propagates, flushing others") {
        SECTION("Others not throwing") {
            out1.throw_end_processing_on_next();
            REQUIRE_THROWS_AS(in.handle(tc_event{101, -3, 123}),
                              end_of_processing);
            REQUIRE(out0.check_flushed());
            REQUIRE(out2.check_flushed());
        }

        SECTION("Other throwing error") {
            out1.throw_end_processing_on_next();
            out2.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.handle(tc_event{101, -3, 123}),
                              std::runtime_error);
            REQUIRE(out0.check_flushed());
        }

        SECTION("Other throwing end") {
            out1.throw_end_processing_on_next();
            out2.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.handle(tc_event{101, -3, 123}),
                              end_of_processing);
            REQUIRE(out0.check_flushed());
        }
    }
}

TEST_CASE("Route with heterogeneous downstreams") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        route<type_list<e0>, type_list<>>(
            []([[maybe_unused]] e0 const &event) { return std::size_t(0); },
            capture_output<type_list<e0>>(
                ctx->tracker<capture_output_access>("out0")),
            null_sink()));
    in.require_output_checked(ctx, "out0");
    auto out0 = capture_output_checker<type_list<e0>>(valcat, ctx, "out0");

    in.handle(e0{});
    REQUIRE(out0.check(emitted_as::same_as_fed, e0{}));
    in.flush();
    REQUIRE(out0.check_flushed());
}

TEST_CASE("Broadcast") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, broadcast<type_list<e0>>(
                    capture_output<type_list<e0>>(
                        ctx->tracker<capture_output_access>("out0")),
                    capture_output<type_list<e0>>(
                        ctx->tracker<capture_output_access>("out1")),
                    capture_output<type_list<e0>>(
                        ctx->tracker<capture_output_access>("out2"))));
    in.require_output_checked(ctx, "out0");
    in.require_output_checked(ctx, "out1");
    in.require_output_checked(ctx, "out2");
    auto out0 = capture_output_checker<type_list<e0>>(valcat, ctx, "out0");
    auto out1 = capture_output_checker<type_list<e0>>(valcat, ctx, "out1");
    auto out2 = capture_output_checker<type_list<e0>>(valcat, ctx, "out2");

    SECTION("Empty stream") {
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
        REQUIRE(out2.check_flushed());
    }

    SECTION("Events are broadcast") {
        in.handle(e0{});
        REQUIRE(out0.check(emitted_as::any_allowed, e0{}));
        REQUIRE(out1.check(emitted_as::any_allowed, e0{}));
        REQUIRE(out2.check(emitted_as::any_allowed, e0{}));
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
        REQUIRE(out2.check_flushed());
    }

    SECTION("Error on output propagates without flushing others") {
        out1.throw_error_on_next();
        REQUIRE_THROWS(in.handle(e0{}));
        REQUIRE(out0.check(e0{})); // Received before out1 threw
        REQUIRE(out0.check_not_flushed());
        REQUIRE(out2.check_not_flushed());
    }

    SECTION("End on output propagates, flushing others") {
        SECTION("Others not throwing") {
            out1.throw_end_processing_on_next();
            REQUIRE_THROWS_AS(in.handle(e0{}), end_of_processing);
            REQUIRE(out0.check(e0{})); // Received before out1 threw
            REQUIRE(out0.check_flushed());
            REQUIRE(out2.check_flushed());
        }

        SECTION("Other throwing error") {
            out1.throw_end_processing_on_next();
            out2.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.handle(e0{}), std::runtime_error);
            REQUIRE(out0.check(e0{})); // Received before out1 threw
            REQUIRE(out0.check_flushed());
        }

        SECTION("Other throwing end") {
            out1.throw_end_processing_on_next();
            out2.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.handle(e0{}), end_of_processing);
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
            REQUIRE_THROWS_AS(in.flush(), end_of_processing);
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
            REQUIRE_THROWS_AS(in.flush(), end_of_processing);
            REQUIRE(out0.check_flushed());
        }
    }
}

} // namespace tcspc
