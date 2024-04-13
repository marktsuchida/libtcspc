/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/batch.hpp"

#include "libtcspc/bucket.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <memory>
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

TEST_CASE("introspect batch, unbatch", "[introspect]") {
    check_introspect_simple_processor(
        batch<int>(new_delete_bucket_source<int>::create(), 1, null_sink()));
    check_introspect_simple_processor(unbatch<int>(null_sink()));
}

TEST_CASE("batch") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<int>>(
        batch<int>(new_delete_bucket_source<int>::create(), 3,
                   capture_output<type_list<bucket<int>>>(
                       ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<bucket<int>>>(
        ctx->access<capture_output_access>("out"));

    SECTION("ending mid-batch") {
        in.feed(42);
        in.feed(43);
        in.feed(44);
        REQUIRE(out.check(tmp_bucket<int>({42, 43, 44})));
        in.feed(45);
        in.flush();
        REQUIRE(out.check(tmp_bucket<int>({45})));
        REQUIRE(out.check_flushed());
    }

    SECTION("ending in full batch") {
        in.feed(42);
        in.feed(43);
        in.feed(44);
        REQUIRE(out.check(tmp_bucket<int>({42, 43, 44})));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

struct move_out_sink {
    template <typename T> void handle(T &&t) {
        [[maybe_unused]] T u = std::forward<T>(t);
    }
    void flush() {}
};

TEST_CASE("unbatch lvalue and rvalue correctly") {
    auto ctx = std::make_shared<processor_context>();
    auto proc = unbatch<std::unique_ptr<int>>(move_out_sink());
    std::vector<std::unique_ptr<int>> v;
    v.push_back(std::make_unique<int>(42));

    SECTION("container lvalue ref") {
        proc.handle(v);
        REQUIRE(v[0]);
        REQUIRE(*v[0] == 42);
    }

    SECTION("container lvalue ref with mutable elements") {
        auto s = span(v);
        proc.handle(s);
        REQUIRE(v[0]);
        REQUIRE(*v[0] == 42);
    }

    SECTION("container rvalue ref with mutable elements") {
        proc.handle(std::move(v));
        REQUIRE_FALSE(v[0]); // NOLINT(bugprone-use-after-move)
    }

    SECTION("container rvalue ref with const elements") {
        proc.handle(span(std::as_const(v)));
        REQUIRE(v[0]);
        REQUIRE(*v[0] == 42);
    }
}

TEST_CASE("unbatch") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<pvector<int>>>(
        unbatch<int>(capture_output<type_list<int>>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(
        ctx->access<capture_output_access>("out"));

    in.feed(pvector<int>{42, 43, 44});
    REQUIRE(out.check(42));
    REQUIRE(out.check(43));
    REQUIRE(out.check(44));
    in.feed(pvector<int>{});
    in.feed(pvector<int>{});
    in.feed(pvector<int>{45});
    REQUIRE(out.check(45));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("process_in_batches") {
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<type_list<int>>(process_in_batches<int>(
        3, capture_output<type_list<int>>(
               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<type_list<int>>(
        ctx->access<capture_output_access>("out"));

    in.feed(42);
    in.feed(43);
    in.feed(44);
    REQUIRE(out.check(42));
    REQUIRE(out.check(43));
    REQUIRE(out.check(44));
    in.feed(45);
    in.flush();
    REQUIRE(out.check(45));
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
