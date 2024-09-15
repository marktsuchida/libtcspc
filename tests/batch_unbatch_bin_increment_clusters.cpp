/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/batch_unbatch_bin_increment_clusters.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tcspc {

TEST_CASE("type constraints: batch_bin_increment_clusters") {
    using proc_type = decltype(batch_bin_increment_clusters(
        new_delete_bucket_source<u16>::create(), arg::bucket_size<>{100},
        arg::batch_size<>{50}, sink_events<bucket<u16>>()));
    STATIC_CHECK(is_processor_v<proc_type, bin_increment_cluster_event<>>);
    STATIC_CHECK_FALSE(is_processor_v<proc_type, int>);
}

TEST_CASE("type constraints: unbatch_bin_increment_clusters") {
    struct e0 {};
    using proc_type = decltype(unbatch_bin_increment_clusters(
        sink_events<bin_increment_cluster_event<>, e0>()));
    STATIC_CHECK(is_processor_v<proc_type, bucket<u16>>);
    STATIC_CHECK_FALSE(is_processor_v<proc_type, bucket<e0>>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
    STATIC_CHECK(handles_event_v<proc_type, e0>);
}

TEST_CASE(
    "introspect: batch_bin_increment_clusters, unbatch_bin_increment_clusters") {
    check_introspect_simple_processor(batch_bin_increment_clusters(
        new_delete_bucket_source<u16>::create(), arg::bucket_size<>{100},
        arg::batch_size<>{50}, null_sink()));
}

TEST_CASE("batch_bin_increment_clusters") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         batch_bin_increment_clusters(
                             new_delete_bucket_source<u16>::create(),
                             arg::bucket_size<>{256}, arg::batch_size<>{0},
                             capture_output<type_list<bucket<u16>>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<bucket<u16>>>(valcat, ctx, "out");

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({42, 43, 44})});
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({5, 6, 7})});
    in.flush();
    REQUIRE(out.check(emitted_as::always_rvalue,
                      test_bucket<u16>({3, 42, 43, 44, 3, 5, 6, 7})));
    REQUIRE(out.check_flushed());
}

TEST_CASE("batch_bin_increment_clusters handles full bucket") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         batch_bin_increment_clusters(
                             new_delete_bucket_source<u16>::create(),
                             arg::bucket_size<>{5}, arg::batch_size<>{0},
                             capture_output<type_list<bucket<u16>>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<bucket<u16>>>(valcat, ctx, "out");

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({42, 43, 44})});
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({5, 6, 7})});
    REQUIRE(out.check(emitted_as::always_rvalue,
                      test_bucket<u16>({3, 42, 43, 44})));
    in.flush();
    REQUIRE(
        out.check(emitted_as::always_rvalue, test_bucket<u16>({3, 5, 6, 7})));
    REQUIRE(out.check_flushed());
}

TEST_CASE("batch_bin_increment_clusters handles batch size") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         batch_bin_increment_clusters(
                             new_delete_bucket_source<u16>::create(),
                             arg::bucket_size<>{256}, arg::batch_size<>{1},
                             capture_output<type_list<bucket<u16>>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<bucket<u16>>>(valcat, ctx, "out");

    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({42, 43, 44})});
    REQUIRE(out.check(emitted_as::always_rvalue,
                      test_bucket<u16>({3, 42, 43, 44})));
    in.handle(bin_increment_cluster_event<>{test_bucket<u16>({5, 6, 7})});
    REQUIRE(
        out.check(emitted_as::always_rvalue, test_bucket<u16>({3, 5, 6, 7})));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("unbatch_bin_increment_clusters") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, unbatch_bin_increment_clusters(
                    capture_output<type_list<bin_increment_cluster_event<>>>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<bin_increment_cluster_event<>>>(
            valcat, ctx, "out");

    in.handle(test_bucket<u16>({0, 0, 0}));
    REQUIRE(out.check(emitted_as::always_lvalue,
                      bin_increment_cluster_event<>{test_bucket<u16>({})}));
    REQUIRE(out.check(emitted_as::always_lvalue,
                      bin_increment_cluster_event<>{test_bucket<u16>({})}));
    REQUIRE(out.check(emitted_as::always_lvalue,
                      bin_increment_cluster_event<>{test_bucket<u16>({})}));
    in.handle(test_bucket<u16>({3, 42, 43, 44}));
    REQUIRE(out.check(
        emitted_as::always_lvalue,
        bin_increment_cluster_event<>{test_bucket<u16>({42, 43, 44})}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
