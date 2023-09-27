/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/merge.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_erased_processor.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;
using e2 = timestamped_test_event<2>;
using e3 = timestamped_test_event<3>;
using all_events = event_set<e0, e1, e2, e3>;

TEST_CASE("Merge", "[merge]") {
    // The two merge inputs have different types. Wrap them so they can be
    // swapped for symmetric tests.
    // in_0 -> ref -> min0 -> merge_impl -> out
    // in_x -> min_x -> ref -> min0/1 -> merge_impl -> out
    //         erased

    auto ctx = std::make_shared<processor_context>();
    auto [min0, min1] = merge<all_events>(
        1024, capture_output<all_events>(
                  ctx->tracker<capture_output_access>("out")));

    auto min_x = type_erased_processor<all_events>(ref_processor(min0));
    auto min_y = type_erased_processor<all_events>(ref_processor(min1));
    int const x = GENERATE(0, 1);
    if (x != 0) {
        using std::swap;
        swap(min_x, min_y);
    }

    auto in_0 = feed_input<all_events>(ref_processor(min0));
    in_0.require_output_checked(ctx, "out");
    auto in_1 = feed_input<all_events>(ref_processor(min1));
    in_1.require_output_checked(ctx, "out");
    auto in_x = feed_input<all_events>(std::move(min_x));
    in_x.require_output_checked(ctx, "out");
    auto in_y = feed_input<all_events>(std::move(min_y));
    in_y.require_output_checked(ctx, "out");

    auto out = capture_output_checker<all_events>(
        ctx->accessor<capture_output_access>("out"));

    SECTION("Empty yields empty") {
        in_x.flush();
        REQUIRE(out.check_not_flushed());
        in_y.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Events from in_0 emitted before those from in_1") {
        in_1.feed(e1{42});
        in_0.feed(e0{42});
        REQUIRE(out.check(e0{42}));
        in_1.feed(e3{42});
        in_0.feed(e2{42});
        REQUIRE(out.check(e2{42}));

        SECTION("End in_0 first") {
            in_0.flush();
            REQUIRE(out.check(e1{42}));
            REQUIRE(out.check(e3{42}));
            REQUIRE(out.check_not_flushed());
            in_1.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("End in_1 first") {
            in_1.flush();
            REQUIRE(out.check_not_flushed());
            in_0.flush();
            REQUIRE(out.check(e1{42}));
            REQUIRE(out.check(e3{42}));
            REQUIRE(out.check_flushed());
        }
    }

    SECTION("Events in abstime order") {
        in_x.feed(e0{1});
        in_y.feed(e1{2});
        REQUIRE(out.check(e0{1}));
        in_x.feed(e0{3});
        REQUIRE(out.check(e1{2}));

        SECTION("End in_x first") {
            in_x.flush();
            REQUIRE(out.check_not_flushed());
            in_y.flush();
            REQUIRE(out.check(e0{3}));
            REQUIRE(out.check_flushed());
        }

        SECTION("End in_x, additional y input") {
            in_x.flush();
            in_y.feed(e1{4});
            REQUIRE(out.check(e0{3}));
            REQUIRE(out.check(e1{4}));
            in_y.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("End in_y first") {
            in_y.flush();
            REQUIRE(out.check(e0{3}));
            REQUIRE(out.check_not_flushed());
            in_x.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("End in_y, additional x input") {
            in_y.flush();
            REQUIRE(out.check(e0{3}));
            in_x.feed(e0{4});
            REQUIRE(out.check(e0{4}));
            in_x.flush();
            REQUIRE(out.check_flushed());
        }
    }

    SECTION("Delayed on in_x") {
        in_x.feed(e0{2});
        in_y.feed(e1{1});
        REQUIRE(out.check(e1{1}));
        in_x.feed(e0{4});
        in_y.feed(e1{3});
        REQUIRE(out.check(e0{2}));
        REQUIRE(out.check(e1{3}));

        SECTION("End in_x first") {
            in_x.flush();
            REQUIRE(out.check_not_flushed());
            in_y.flush();
            REQUIRE(out.check(e0{4}));
            REQUIRE(out.check_flushed());
        }

        SECTION("End in_y first") {
            in_y.flush();
            REQUIRE(out.check(e0{4}));
            REQUIRE(out.check_not_flushed());
            in_x.flush();
            REQUIRE(out.check_flushed());
        }
    }
}

TEST_CASE("merge N streams", "[merge_n]") {
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
            ctx->accessor<capture_output_access>("out"));

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

TEST_CASE("merge unsorted", "[merge_n_unsorted]") {
    auto ctx = std::make_shared<processor_context>();
    auto [min0, min1] = merge_n_unsorted(capture_output<all_events>(
        ctx->tracker<capture_output_access>("out")));
    auto in0 = feed_input<all_events>(std::move(min0));
    auto in1 = feed_input<all_events>(std::move(min1));
    in0.require_output_checked(ctx, "out");
    in1.require_output_checked(ctx, "out");
    auto out = capture_output_checker<all_events>(
        ctx->accessor<capture_output_access>("out"));

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
