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

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

static_assert(handles_flush_v<type_erased_processor<type_list<>>>);
static_assert(not handles_event_v<type_erased_processor<type_list<>>, e0>);
static_assert(
    handles_events_v<type_erased_processor<type_list<e0>>, type_list<e0>>);
static_assert(handles_flush_v<type_erased_processor<type_list<e0>>>);
static_assert(handles_events_v<type_erased_processor<type_list<e0, e1>>,
                               type_list<e0, e1>>);
static_assert(handles_flush_v<type_erased_processor<type_list<e0, e1>>>);

namespace internal {

// handles_events_v works even if the functions are virtual.
static_assert(handles_flush_v<abstract_processor<type_list<>>>);
static_assert(not handles_event_v<abstract_processor<type_list<>>, e0>);
static_assert(
    handles_events_v<abstract_processor<type_list<e0>>, type_list<e0>>);
static_assert(handles_flush_v<abstract_processor<type_list<e0>>>);
static_assert(handles_events_v<abstract_processor<type_list<e0, e1>>,
                               type_list<e0, e1>>);
static_assert(handles_flush_v<abstract_processor<type_list<e0, e1>>>);

static_assert(
    handles_flush_v<virtual_processor<sink_events<type_list<>>, type_list<>>>);
static_assert(not handles_event_v<
              virtual_processor<sink_events<type_list<>>, type_list<>>, e0>);
static_assert(handles_events_v<
              virtual_processor<sink_events<type_list<e0>>, type_list<e0>>,
              type_list<e0>>);
static_assert(handles_flush_v<
              virtual_processor<sink_events<type_list<e0>>, type_list<e0>>>);
static_assert(
    handles_events_v<
        virtual_processor<sink_events<type_list<e0, e1>>, type_list<e0, e1>>,
        type_list<e0, e1>>);
static_assert(handles_flush_v<virtual_processor<sink_events<type_list<e0, e1>>,
                                                type_list<e0, e1>>>);

} // namespace internal

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

} // namespace tcspc
