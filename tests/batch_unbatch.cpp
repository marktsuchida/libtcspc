/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/batch_unbatch.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace tcspc {

TEST_CASE("type constraints: batch") {
    struct e0 {};
    struct e1 {};
    using proc_type =
        decltype(batch<e0>(new_delete_bucket_source<e0>::create(),
                           arg::batch_size<>{100}, sink_events<bucket<e0>>()));
    STATIC_CHECK(is_processor_v<proc_type, e0>);
    STATIC_CHECK_FALSE(is_processor_v<proc_type, e1>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bucket<e0>>);
}

TEST_CASE("type constraints: unbatch") {
    struct e0 {};
    using proc_type = decltype(unbatch<bucket<int>>(sink_events<int, e0>()));
    STATIC_CHECK(is_processor_v<proc_type, bucket<int>>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bucket<short>>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bucket<e0>>);
    STATIC_CHECK(handles_event_v<proc_type, int>);
    STATIC_CHECK(handles_event_v<proc_type, e0>);
    using const_proc_type =
        decltype(unbatch<bucket<int const>>(sink_events<int, e0>()));
    STATIC_CHECK(handles_event_v<const_proc_type, bucket<int const>>);
}

TEST_CASE("type constraints: process_in_batches") {
    struct e0 {};
    using proc_type = decltype(process_in_batches<e0>(arg::batch_size<>{1},
                                                      sink_events<e0>()));
    STATIC_CHECK(is_processor_v<proc_type, e0>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("introspect: batch, unbatch") {
    check_introspect_simple_processor(
        batch<int>(new_delete_bucket_source<int>::create(),
                   arg::batch_size<>{1}, null_sink()));
    check_introspect_simple_processor(unbatch<bucket<int>>(null_sink()));
}

TEST_CASE("batch") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, batch<int>(new_delete_bucket_source<int>::create(),
                           arg::batch_size<>{3},
                           capture_output<type_list<bucket<int>>>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<bucket<int>>>(valcat, ctx, "out");

    SECTION("ending mid-batch") {
        in.handle(42);
        in.handle(43);
        in.handle(44);
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket({42, 43, 44})));
        in.handle(45);
        in.flush();
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket({45})));
        REQUIRE(out.check_flushed());
    }

    SECTION("ending in full batch") {
        in.handle(42);
        in.handle(43);
        in.handle(44);
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket({42, 43, 44})));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

namespace {

struct move_out_sink {
    template <typename T> void handle(T &&t) {
        [[maybe_unused]] T u = std::forward<T>(t);
    }
    void flush() {}
};

} // namespace

TEST_CASE("unbatch") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, unbatch<std::vector<int>>(capture_output<type_list<int>>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(valcat, ctx, "out");

    in.handle(std::vector<int>{42, 43, 44});
    REQUIRE(out.check(emitted_as::same_as_fed, 42));
    REQUIRE(out.check(emitted_as::same_as_fed, 43));
    REQUIRE(out.check(emitted_as::same_as_fed, 44));
    in.handle(std::vector<int>{});
    in.handle(std::vector<int>{});
    in.handle(std::vector<int>{45});
    REQUIRE(out.check(emitted_as::same_as_fed, 45));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("unbatch const") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, unbatch<span<int const>>(capture_output<type_list<int>>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(valcat, ctx, "out");

    in.handle(span<int const>({42, 43, 44}));
    REQUIRE(out.check(emitted_as::always_lvalue, 42));
    REQUIRE(out.check(emitted_as::always_lvalue, 43));
    REQUIRE(out.check(emitted_as::always_lvalue, 44));
    in.handle(span<int const>({}));
    in.handle(span<int const>({}));
    in.handle(span<int const>({45}));
    REQUIRE(out.check(emitted_as::always_lvalue, 45));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("process_in_batches") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         process_in_batches<int>(
                             arg::batch_size<>{3},
                             capture_output<type_list<int>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(valcat, ctx, "out");

    in.handle(42);
    in.handle(43);
    in.handle(44);
    REQUIRE(out.check(emitted_as::always_rvalue, 42));
    REQUIRE(out.check(emitted_as::always_rvalue, 43));
    REQUIRE(out.check(emitted_as::always_rvalue, 44));
    in.handle(45);
    in.flush();
    REQUIRE(out.check(emitted_as::always_rvalue, 45));
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
