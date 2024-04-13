/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/batch_unbatch_from_bytes.hpp"

#include "libtcspc/bucket.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstring>
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

TEST_CASE("introspect batch_from_bytes, unbatch_from_bytes", "[introspect]") {
    check_introspect_simple_processor(batch_from_bytes<int>(
        new_delete_bucket_source<int>::create(), null_sink()));
    check_introspect_simple_processor(unbatch_from_bytes<int>(null_sink()));
}

TEST_CASE("batch_from_bytes") {
    auto ctx = std::make_shared<processor_context>();
    auto in =
        feed_input<type_list<span<std::byte const>>>(batch_from_bytes<int>(
            new_delete_bucket_source<int>::create(),
            capture_output<type_list<bucket<int>>>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<bucket<int>>>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty input does not emitted batch") {
        in.feed(span<std::byte const>());
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("input with whole elements is emitted exactly") {
        std::vector<int> data{1, 2, 3};
        in.feed(as_bytes(span(data)));
        REQUIRE(out.check(tmp_bucket<int>({1, 2, 3})));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("split elements are emitted in next batch") {
        std::vector<int> data{1, 2, 3};
        in.feed(as_bytes(span(data)).subspan(0, 5));
        REQUIRE(out.check(tmp_bucket<int>({1})));
        in.feed(as_bytes(span(data)).subspan(5));
        REQUIRE(out.check(tmp_bucket<int>({2, 3})));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("element can be split over more than 2 inputs") {
        std::vector<int> data{42};
        in.feed(as_bytes(span(data)).subspan(0, 1));
        in.feed(as_bytes(span(data)).subspan(1, 1));
        in.feed(as_bytes(span(data)).subspan(2, 0));
        in.feed(as_bytes(span(data)).subspan(2));
        REQUIRE(out.check(tmp_bucket<int>({42})));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("flush throws if buffered bytes remain") {
        std::vector<std::byte> data;
        data.resize(sizeof(int) - 1);
        in.feed(span(std::as_const(data)));
        REQUIRE_THROWS_AS(in.flush(), std::runtime_error);
    }
}

TEST_CASE("unbatch_from_bytes") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<span<std::byte const>>>(
        unbatch_from_bytes<int>(capture_output<type_list<int>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty input does not emit events") {
        in.feed(span<std::byte const>());
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("aligned input with whole elements emits exactly") {
        std::vector<int> data{1, 2, 3};
        in.feed(as_bytes(span(data)));
        REQUIRE(out.check(1));
        REQUIRE(out.check(2));
        REQUIRE(out.check(3));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("unaligned input with whole elements emits exactly") {
        std::vector<int> data{1, 2, 3};
        std::array<int, 4> buf{};
        std::memcpy(as_writable_bytes(span(buf)).subspan(2).data(),
                    span(data).data(), sizeof(int) * data.size());
        in.feed(as_bytes(span(buf)).subspan(2, sizeof(int) * data.size()));
        REQUIRE(out.check(1));
        REQUIRE(out.check(2));
        REQUIRE(out.check(3));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("split elements are emitted upon next input") {
        std::vector<int> data{1, 2, 3};
        in.feed(as_bytes(span(data)).subspan(0, 5));
        REQUIRE(out.check(1));
        in.feed(as_bytes(span(data)).subspan(5));
        REQUIRE(out.check(2));
        REQUIRE(out.check(3));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("element can be split over more than 2 inputs") {
        std::vector<int> data{42};
        in.feed(as_bytes(span(data)).subspan(0, 1));
        in.feed(as_bytes(span(data)).subspan(1, 1));
        in.feed(as_bytes(span(data)).subspan(2, 0));
        in.feed(as_bytes(span(data)).subspan(2));
        REQUIRE(out.check(42));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("flush throws if buffered bytes remain") {
        std::vector<std::byte> data;
        data.resize(sizeof(int) - 1);
        in.feed(span(std::as_const(data)));
        REQUIRE_THROWS_AS(in.flush(), std::runtime_error);
    }
}

} // namespace tcspc
