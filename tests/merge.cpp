/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/merge.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/introspect.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_erased_processor.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <set>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace tcspc {

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;
using e2 = timestamped_test_event<2>;
using e3 = timestamped_test_event<3>;
using all_events = type_list<e0, e1, e2, e3>;

namespace {

// Like check_introspect_simple_processor, but for merge input.
template <typename MI>
auto check_introspect_merge_input(MI const &input,
                                  std::string_view impl_name) {
    auto info = check_introspect_node_info(input);

    auto const g = input.introspect_graph();
    CHECK(g.nodes().size() == 3);
    CHECK(g.entry_points().size() == 1);
    auto const node = g.entry_points()[0];
    CHECK(g.node_info(node) == info);
    CHECK(g.edges().size() == 2);
    auto const impl_node = [&] {
        for (auto const &e : g.edges()) {
            if (e.first == node)
                return e.second;
        }
        throw;
    }();
    CHECK(g.node_info(impl_node).name() == impl_name);
    auto const sink_node = [&] {
        for (auto const &e : g.edges()) {
            if (e.first == impl_node)
                return e.second;
        }
        throw;
    }();
    CHECK(g.node_info(sink_node).name() == "null_sink");
    return info;
}

} // namespace

TEST_CASE("introspect merge", "[introspect]") {
    auto const [m0, m1] = merge<all_events>(1, null_sink());
    check_introspect_merge_input(m0, "merge_impl");
    check_introspect_merge_input(m1, "merge_impl");

    auto const [n0, n1, n2, n3, n4] = merge_n<5, all_events>(1, null_sink());
    std::set<processor_node_id> unique_nodes;
    std::set<std::pair<processor_node_id, processor_node_id>> unique_edges;
    auto add_to_sets = [&](auto const &n) {
        auto const g = n.introspect_graph();
        auto const ns = g.nodes();
        unique_nodes.insert(ns.begin(), ns.end());
        auto const es = g.edges();
        unique_edges.insert(es.begin(), es.end());
    };
    add_to_sets(n0);
    add_to_sets(n1);
    add_to_sets(n2);
    add_to_sets(n3);
    add_to_sets(n4);
    CHECK(unique_nodes.size() == 13); // 4 merge_impl, 8 merge_input, null_sink
    CHECK(unique_edges.size() == 12); // 4 merge_impl, 8 merge_input

    auto const [u0, u1, u2, u3, u4] = merge_n_unsorted<5>(null_sink());
    check_introspect_merge_input(u0, "merge_unsorted_impl");
    check_introspect_merge_input(u1, "merge_unsorted_impl");
    check_introspect_merge_input(u2, "merge_unsorted_impl");
    check_introspect_merge_input(u3, "merge_unsorted_impl");
    check_introspect_merge_input(u4, "merge_unsorted_impl");
}

TEST_CASE("Merge") {
    auto ctx = std::make_shared<processor_context>();

    SECTION("asymmetric tests") {
        auto [mi0, mi1] = merge<all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out")));
        auto in0 = feed_input<all_events>(std::move(mi0));
        in0.require_output_checked(ctx, "out");
        auto in1 = feed_input<all_events>(std::move(mi1));
        in1.require_output_checked(ctx, "out");
        auto out = capture_output_checker<all_events>(
            ctx->access<capture_output_access>("out"));

        SECTION("events from input 0 emitted before those from input 1") {
            in1.feed(e1{42});
            in0.feed(e0{42});
            REQUIRE(out.check(e0{42}));
            in1.feed(e3{42});
            in0.feed(e2{42});
            REQUIRE(out.check(e2{42}));

            SECTION("end input 0 first") {
                in0.flush();
                REQUIRE(out.check(e1{42}));
                REQUIRE(out.check(e3{42}));
                REQUIRE(out.check_not_flushed());
                in1.flush();
                REQUIRE(out.check_flushed());
            }

            SECTION("end input 1 first") {
                in1.flush();
                REQUIRE(out.check_not_flushed());
                in0.flush();
                REQUIRE(out.check(e1{42}));
                REQUIRE(out.check(e3{42}));
                REQUIRE(out.check_flushed());
            }
        }
    }

    SECTION("symmetric tests") {
        auto [mi0, mi1] = merge<all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out")));
        auto temi0 = type_erased_processor<all_events>(std::move(mi0));
        auto temi1 = type_erased_processor<all_events>(std::move(mi1));
        int const x = GENERATE(0, 1);
        if (x != 0) {
            using std::swap;
            swap(temi0, temi1);
        }
        auto in_x = feed_input<all_events>(std::move(temi0));
        auto in_y = feed_input<all_events>(std::move(temi1));
        in_x.require_output_checked(ctx, "out");
        in_y.require_output_checked(ctx, "out");
        auto out = capture_output_checker<all_events>(
            ctx->access<capture_output_access>("out"));

        SECTION("empty yields empty") {
            in_x.flush();
            REQUIRE(out.check_not_flushed());
            in_y.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("events in abstime order") {
            in_x.feed(e0{1});
            in_y.feed(e1{2});
            REQUIRE(out.check(e0{1}));
            in_x.feed(e0{3});
            REQUIRE(out.check(e1{2}));

            SECTION("end in_x first") {
                in_x.flush();
                REQUIRE(out.check_not_flushed());
                in_y.flush();
                REQUIRE(out.check(e0{3}));
                REQUIRE(out.check_flushed());
            }

            SECTION("end in_x, additional y input") {
                in_x.flush();
                in_y.feed(e1{4});
                REQUIRE(out.check(e0{3}));
                REQUIRE(out.check(e1{4}));
                in_y.flush();
                REQUIRE(out.check_flushed());
            }

            SECTION("end in_y first") {
                in_y.flush();
                REQUIRE(out.check(e0{3}));
                REQUIRE(out.check_not_flushed());
                in_x.flush();
                REQUIRE(out.check_flushed());
            }

            SECTION("end in_y, additional x input") {
                in_y.flush();
                REQUIRE(out.check(e0{3}));
                in_x.feed(e0{4});
                REQUIRE(out.check(e0{4}));
                in_x.flush();
                REQUIRE(out.check_flushed());
            }
        }

        SECTION("delayed on in_x") {
            in_x.feed(e0{2});
            in_y.feed(e1{1});
            REQUIRE(out.check(e1{1}));
            in_x.feed(e0{4});
            in_y.feed(e1{3});
            REQUIRE(out.check(e0{2}));
            REQUIRE(out.check(e1{3}));

            SECTION("end in_x first") {
                in_x.flush();
                REQUIRE(out.check_not_flushed());
                in_y.flush();
                REQUIRE(out.check(e0{4}));
                REQUIRE(out.check_flushed());
            }

            SECTION("end in_y first") {
                in_y.flush();
                REQUIRE(out.check(e0{4}));
                REQUIRE(out.check_not_flushed());
                in_x.flush();
                REQUIRE(out.check_flushed());
            }
        }
    }
}

TEST_CASE("merge single event type") {
    using one_event = type_list<e0>;
    auto ctx = std::make_shared<processor_context>();
    auto [min0, min1] = merge<one_event>(
        1024,
        capture_output<one_event>(ctx->tracker<capture_output_access>("out")));
    auto in0 = feed_input<one_event>(std::move(min0));
    in0.require_output_checked(ctx, "out");
    auto in1 = feed_input<one_event>(std::move(min1));
    in1.require_output_checked(ctx, "out");
    auto out = capture_output_checker<one_event>(
        ctx->access<capture_output_access>("out"));

    // The only non-common code for the single-event-type case is in
    // emit_pending, so lightly test that.

    in0.feed(e0{42});
    in1.feed(e0{41});
    REQUIRE(out.check(e0{41}));
    in1.feed(e0{43});
    REQUIRE(out.check(e0{42}));
    in1.feed(e0{44});
    in1.feed(e0{45});
    in0.feed(e0{46});
    REQUIRE(out.check(e0{43}));
    REQUIRE(out.check(e0{44}));
    REQUIRE(out.check(e0{45}));
    in0.flush();
    in1.flush();
    REQUIRE(out.check(e0{46}));
    REQUIRE(out.check_flushed());
}

TEST_CASE("merge N streams") {
    auto ctx = std::make_shared<processor_context>();

    SECTION("Zero-stream merge_n returns empty tuple") {
        // NOLINTBEGIN(clang-analyzer-deadcode.DeadStores)
        auto tup = merge_n<0, all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out")));
        static_assert(std::tuple_size_v<decltype(tup)> == 0);
        // NOLINTEND(clang-analyzer-deadcode.DeadStores)
    }

    SECTION("Single-stream merge_n returns downstream in tuple") {
        auto [m0] = merge_n<1, all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out")));
        static_assert(
            std::is_same_v<decltype(m0),
                           decltype(capture_output<all_events>(
                               ctx->tracker<capture_output_access>("out")))>);
        auto in = feed_input<all_events>(std::move(m0));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<all_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(e0{0});
        REQUIRE(out.check(e0{0}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Multi-stream merge_n can be instantiated") {
        auto [m0, m1] = merge_n<2, all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out2")));
        auto [n0, n1, n2] = merge_n<3, all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out3")));
        auto [o0, o1, o2, o3] = merge_n<4, all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out4")));
        auto [p0, p1, p2, p3, p4] = merge_n<5, all_events>(
            1024, capture_output<all_events>(
                      ctx->tracker<capture_output_access>("out5")));
    }
}

TEST_CASE("merge unsorted") {
    auto ctx = std::make_shared<processor_context>();
    auto [min0, min1] = merge_n_unsorted(capture_output<all_events>(
        ctx->tracker<capture_output_access>("out")));
    auto in0 = feed_input<all_events>(std::move(min0));
    auto in1 = feed_input<all_events>(std::move(min1));
    in0.require_output_checked(ctx, "out");
    in1.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_events>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty yields empty") {
        in0.flush();
        REQUIRE(out.check_not_flushed());
        in1.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("no buffering, independent flushing") {
        in0.feed(e0{});
        REQUIRE(out.check(e0{}));
        in1.feed(e1{});
        REQUIRE(out.check(e1{}));
        in1.flush();
        in0.feed(e2{});
        REQUIRE(out.check(e2{}));
        in0.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
