/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/batch_unbatch_from_bytes.hpp"

#include "libtcspc/bucket.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tcspc {

namespace {

template <typename T, typename U>
auto tmp_bucket(std::initializer_list<U> il) {
    static auto src = new_delete_bucket_source<T>::create();
    auto b = src->bucket_of_size(il.size());
    std::copy(il.begin(), il.end(), b.begin());
    return b;
}

} // namespace

TEST_CASE("type constraints: batch_from_bytes") {
    using proc_type = decltype(batch_from_bytes<int>(
        new_delete_bucket_source<int>::create(), sink_events<bucket<int>>()));
    STATIC_CHECK(is_processor_v<proc_type, span<std::byte>>);
    STATIC_CHECK(is_processor_v<proc_type, span<std::byte const>>);
    STATIC_CHECK(is_processor_v<proc_type, std::array<std::byte const, 3>>);
    STATIC_CHECK_FALSE(is_processor_v<proc_type, span<short const>>);
}

TEST_CASE("type constraints: unbatch_from_bytes") {
    using proc_type = decltype(unbatch_from_bytes<int>(sink_events<int>()));
    STATIC_CHECK(is_processor_v<proc_type, span<std::byte>>);
    STATIC_CHECK(is_processor_v<proc_type, span<std::byte const>>);
    STATIC_CHECK(is_processor_v<proc_type, std::array<std::byte const, 3>>);
    STATIC_CHECK_FALSE(is_processor_v<proc_type, span<short const>>);
}

TEST_CASE("introspect: batch_from_bytes, unbatch_from_bytes") {
    check_introspect_simple_processor(batch_from_bytes<int>(
        new_delete_bucket_source<int>::create(), null_sink()));
    check_introspect_simple_processor(unbatch_from_bytes<int>(null_sink()));
}

TEST_CASE("batch_from_bytes") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         batch_from_bytes<int>(
                             new_delete_bucket_source<int>::create(),
                             capture_output<type_list<bucket<int>>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<bucket<int>>>(valcat, ctx, "out");

    SECTION("empty input does not emitted batch") {
        in.handle(span<std::byte const>());
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("input with whole elements is emitted exactly") {
        std::vector<int> data{1, 2, 3};
        in.handle(as_bytes(span(data)));
        REQUIRE(
            out.check(emitted_as::always_rvalue, tmp_bucket<int>({1, 2, 3})));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("split elements are emitted in next batch") {
        std::vector<int> data{1, 2, 3};
        in.handle(as_bytes(span(data)).first(5));
        REQUIRE(out.check(emitted_as::always_rvalue, tmp_bucket<int>({1})));
        in.handle(as_bytes(span(data)).subspan(5));
        REQUIRE(out.check(emitted_as::always_rvalue, tmp_bucket<int>({2, 3})));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("element can be split over more than 2 inputs") {
        std::vector<int> data{42};
        in.handle(as_bytes(span(data)).first(1));
        in.handle(as_bytes(span(data)).subspan(1, 1));
        in.handle(as_bytes(span(data)).subspan(2, 0));
        in.handle(as_bytes(span(data)).subspan(2));
        REQUIRE(out.check(emitted_as::always_rvalue, tmp_bucket<int>({42})));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("flush throws if buffered bytes remain") {
        std::vector<std::byte> data;
        data.resize(sizeof(int) - 1);
        in.handle(span(std::as_const(data)));
        REQUIRE_THROWS_AS(in.flush(), std::runtime_error);
    }
}

TEST_CASE("unbatch_from_bytes") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, unbatch_from_bytes<int>(capture_output<type_list<int>>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(valcat, ctx, "out");

    SECTION("empty input does not emit events") {
        in.handle(span<std::byte const>());
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("aligned input with whole elements emits exactly") {
        std::vector<int> data{1, 2, 3};
        in.handle(as_bytes(span(data)));
        REQUIRE(out.check(emitted_as::always_lvalue, 1));
        REQUIRE(out.check(emitted_as::always_lvalue, 2));
        REQUIRE(out.check(emitted_as::always_lvalue, 3));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("unaligned input with whole elements emits exactly") {
        std::vector<int> data{1, 2, 3};
        std::array<int, 4> buf{};
        std::memcpy(as_writable_bytes(span(buf)).subspan(2).data(),
                    span(data).data(), sizeof(int) * data.size());
        in.handle(as_bytes(span(buf)).subspan(2, sizeof(int) * data.size()));
        REQUIRE(out.check(emitted_as::always_lvalue, 1));
        REQUIRE(out.check(emitted_as::always_lvalue, 2));
        REQUIRE(out.check(emitted_as::always_lvalue, 3));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("split elements are emitted upon next input") {
        std::vector<int> data{1, 2, 3};
        in.handle(as_bytes(span(data)).first(5));
        REQUIRE(out.check(emitted_as::always_lvalue, 1));
        in.handle(as_bytes(span(data)).subspan(5));
        REQUIRE(out.check(emitted_as::always_lvalue, 2));
        REQUIRE(out.check(emitted_as::always_lvalue, 3));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("element can be split over more than 2 inputs") {
        std::vector<int> data{42};
        in.handle(as_bytes(span(data)).first(1));
        in.handle(as_bytes(span(data)).subspan(1, 1));
        in.handle(as_bytes(span(data)).subspan(2, 0));
        in.handle(as_bytes(span(data)).subspan(2));
        REQUIRE(out.check(emitted_as::always_lvalue, 42));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("flush throws if buffered bytes remain") {
        std::vector<std::byte> data;
        data.resize(sizeof(int) - 1);
        in.handle(span(std::as_const(data)));
        REQUIRE_THROWS_AS(in.flush(), std::runtime_error);
    }
}

} // namespace tcspc
