/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/type_erased_processor.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/introspect.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

namespace internal {

TEST_CASE("abstract_processor event types") {
    SECTION("handles flush") {
        STATIC_CHECK(handles_flush_v<abstract_processor<type_list<>>>);
        STATIC_CHECK(handles_flush_v<abstract_processor<type_list<e0>>>);
    }

    SECTION("handles events in list only") {
        STATIC_CHECK_FALSE(
            handles_event_v<abstract_processor<type_list<>>, e0>);
        STATIC_CHECK(handles_event_v<abstract_processor<type_list<e0>>, e0>);
        STATIC_CHECK_FALSE(
            handles_event_v<abstract_processor<type_list<e0>>, e1>);
        STATIC_CHECK(handles_events_v<abstract_processor<type_list<e0, e1>>,
                                      type_list<e0, e1>>);
    }
}

TEST_CASE("virtual_processor event types") {
    SECTION("handles flush") {
        STATIC_CHECK(
            handles_flush_v<
                virtual_processor<sink_events<type_list<>>, type_list<>>>);
        STATIC_CHECK(
            handles_flush_v<
                virtual_processor<sink_events<type_list<e0>>, type_list<e0>>>);
    }

    SECTION("handles events in list only") {
        STATIC_CHECK_FALSE(
            handles_event_v<
                virtual_processor<sink_events<type_list<e0>>, type_list<>>,
                e0>);
        STATIC_CHECK(
            handles_event_v<
                virtual_processor<sink_events<type_list<e0>>, type_list<e0>>,
                e0>);
        STATIC_CHECK_FALSE(
            handles_event_v<
                virtual_processor<sink_events<type_list<e0>>, type_list<e0>>,
                e1>);
        STATIC_CHECK(
            handles_events_v<virtual_processor<sink_events<type_list<e0, e1>>,
                                               type_list<e0, e1>>,
                             type_list<e0, e1>>);
    }
}

} // namespace internal

TEST_CASE("type_erased_processor event types") {
    SECTION("handles flush") {
        STATIC_CHECK(
            handles_flush_v<decltype(type_erased_processor<type_list<>>(
                sink_events<type_list<>>()))>);
        STATIC_CHECK(
            handles_flush_v<decltype(type_erased_processor<type_list<e0>>(
                sink_events<type_list<e0>>()))>);
    }

    SECTION("handles events in list only") {
        STATIC_CHECK_FALSE(
            handles_event_v<decltype(type_erased_processor<type_list<>>(
                                sink_events<type_list<e0>>())),
                            e0>);
        STATIC_CHECK(
            handles_event_v<decltype(type_erased_processor<type_list<e0>>(
                                sink_events<type_list<e0>>())),
                            e0>);
        STATIC_CHECK_FALSE(
            handles_event_v<decltype(type_erased_processor<type_list<e0>>(
                                sink_events<type_list<e0>>())),
                            e1>);
        STATIC_CHECK(
            handles_events_v<decltype(type_erased_processor<type_list<e0, e1>>(
                                 sink_events<type_list<e0, e1>>())),
                             type_list<e0, e1>>);
    }
}

TEST_CASE("introspect type_erased_processor", "[introspect]") {
    auto const tep = type_erased_processor<type_list<>>(null_sink());
    auto const info = check_introspect_node_info(tep);
    auto const g = tep.introspect_graph();
    CHECK(g.nodes().size() == 3);
    CHECK(g.entry_points().size() == 1);
    auto const node = g.entry_points()[0];
    CHECK(g.node_info(node) == info);
    CHECK(g.edges().size() == 2);
    auto const virtual_node = [&] {
        for (auto const &e : g.edges()) {
            if (e.first == node)
                return e.second;
        }
        throw;
    }();
    CHECK(g.node_info(virtual_node).name() == "virtual_processor_impl");
    auto const sink_node = [&] {
        for (auto const &e : g.edges()) {
            if (e.first == virtual_node)
                return e.second;
        }
        throw;
    }();
    CHECK(g.node_info(sink_node).name() == "null_sink");
}

TEST_CASE("type_erased_processor move assignment") {
    // Create with stub downstream.
    type_erased_processor<type_list<e0>> tep;

    struct myproc {
        static void handle(e0 const &event) { (void)event; }
        [[nodiscard]] static auto introspect_graph() -> processor_graph {
            return {};
        }
        static void flush() {}
    };

    tep = decltype(tep)(myproc());
}

TEST_CASE("type_erased_processor preserves value category") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        type_erased_processor<type_list<e0>>(capture_output<type_list<e0>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<e0>>(valcat, ctx, "out");

    in.handle(e0{});
    CHECK(out.check(emitted_as::same_as_fed, e0{}));
    in.flush();
    CHECK(out.check_flushed());
}

} // namespace tcspc
