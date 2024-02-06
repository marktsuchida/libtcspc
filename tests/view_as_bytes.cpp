/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/view_as_bytes.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/own_on_copy_view.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_all.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tcspc {

namespace {

using out_events = type_list<own_on_copy_view<std::byte const>>;

}

TEST_CASE("introspect view_as_bytes", "[introspect]") {
    check_introspect_simple_processor(view_as_bytes<int>(null_sink()));
    check_introspect_simple_processor(
        view_histogram_as_bytes<histogram_event<>>(null_sink()));
    check_introspect_simple_processor(
        view_histogram_array_as_bytes<histogram_array_event<>>(null_sink()));
}

TEST_CASE("view as bytes") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<int>>(
        view_as_bytes<int>(capture_output<out_events>(
            ctx->tracker<capture_output_accessor>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_accessor>("out"));

    int i = 42;
    in.feed(i);
    REQUIRE(out.check(own_on_copy_view(as_bytes(span(&i, 1)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("view as bytes, vector specialization") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<std::vector<int>>>(
        view_as_bytes<std::vector<int>>(capture_output<out_events>(
            ctx->tracker<capture_output_accessor>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_accessor>("out"));

    std::vector data{42, 43};
    in.feed(data);
    REQUIRE(out.check(own_on_copy_view(as_bytes(span(data)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("view histogram as bytes") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<histogram_event<>>>(
        view_histogram_as_bytes<histogram_event<>>(capture_output<out_events>(
            ctx->tracker<capture_output_accessor>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_accessor>("out"));

    std::vector<default_data_traits::bin_type> hist{1, 2, 3};
    histogram_event<> const event{
        abstime_range<std::int64_t>{0, 1},
        own_on_copy_view<default_data_traits::bin_type>(hist),
        histogram_stats{}};
    in.feed(event);
    REQUIRE(out.check(own_on_copy_view(as_bytes(span(hist)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("view histogram array as bytes") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<histogram_array_event<>>>(
        view_histogram_array_as_bytes<histogram_array_event<>>(
            capture_output<out_events>(
                ctx->tracker<capture_output_accessor>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_accessor>("out"));

    std::vector<default_data_traits::bin_type> histarr{1, 2, 3};
    histogram_array_event<> const event{
        abstime_range<std::int64_t>{0, 1},
        own_on_copy_view<default_data_traits::bin_type>(histarr),
        histogram_stats{}};
    in.feed(event);
    REQUIRE(out.check(own_on_copy_view(as_bytes(span(histarr)))));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
