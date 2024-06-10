/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/bucket.hpp"

#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"
#include "test_thread_utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <thread>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace tcspc {

TEST_CASE("default-constructed bucket is empty and regular") {
    bucket<int> b;
    bucket<int> b2 = b;
    b2 = b;
    CHECK(b2 == b);

    CHECK(b.begin() == b.end());
    CHECK(b.rbegin() == b.rend());
    CHECK(b.size() == 0); // NOLINT(readability-container-size-empty)
    CHECK(b.size_bytes() == 0);
    CHECK(b.empty());
}

TEST_CASE("non-empty bucket has expected contents") {
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), std::move(v));
    CHECK_FALSE(b.empty());
    CHECK(b.size() == 3);
    CHECK(b.size_bytes() == 3 * sizeof(int));
    CHECK(b[1] == 43);
    CHECK(span(b).size() == 3);
}

TEST_CASE("bucket storage can be observed or extracted") {
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), std::move(v));

    CHECK_THROWS_AS(b.storage<int>(), std::bad_cast);
    CHECK(b.storage<std::vector<int>>()[1] == 43);

    CHECK_THROWS_AS(b.extract_storage<int>(), std::bad_cast);
    auto const vv = b.extract_storage<std::vector<int>>();
    CHECK_THROWS_AS(b.extract_storage<std::vector<int>>(), std::bad_cast);
    CHECK(vv[1] == 43);
    CHECK(b.empty());
}

TEST_CASE("move constructed or assigned bucket transfers storage") {
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), std::move(v));
    auto bb = std::move(b);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_THROWS_AS(b.storage<std::vector<int>>(), std::bad_cast);
    auto const vv = bb.extract_storage<std::vector<int>>();
    CHECK(vv[1] == 43);
}

TEST_CASE("copy constructed or assigned bucket has its own storage") {
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), std::move(v));
    auto bb = b;
    CHECK_THROWS_AS(bb.storage<std::vector<int>>(), std::bad_cast);
    CHECK(bb[1] == 43);
    auto const vv = b.extract_storage<std::vector<int>>();
    CHECK(vv[1] == 43);
}

TEST_CASE("unrelated buckets compare equal if data equal") {
    struct ignore_storage {};
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), ignore_storage{});
    std::array<int, 3> a{42, 43, 44};
    auto bb = bucket<int>(span(a), ignore_storage{});
    CHECK(b == bb);
}

TEST_CASE("bucket can be inserted into stream") {
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), std::move(v));
    std::ostringstream strm;
    strm << b;
}

TEST_CASE("new_delete_bucket_source provides buckets") {
    auto source = new_delete_bucket_source<int>::create();
    CHECK_FALSE(source->supports_shared_views());
    auto b = source->bucket_of_size(3);
    CHECK(b.size() == 3);
    b[0] = 42;
    b[2] = 44;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    auto e = b.extract_storage<std::unique_ptr<int[]>>();
    CHECK(e[0] == 42);
    CHECK(e[2] == 44);
}

TEST_CASE("sharable_new_delete_bucket_source provides sharable buckets") {
    auto source = sharable_new_delete_bucket_source<int>::create();
    CHECK(source->supports_shared_views());
    auto b = source->bucket_of_size(3);
    CHECK(b.size() == 3);
    std::fill(b.begin(), b.end(), 0);
    b[0] = 42;

    SECTION("extracts as shared_ptr") {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        auto e = b.extract_storage<std::shared_ptr<int[]>>();
        CHECK(e[0] == 42);
    }

    SECTION("create view, destroy view, original survives") {
        {
            auto v = source->shared_view_of(b);
            static_assert(std::is_same_v<decltype(v), bucket<int const>>);
            CHECK(v.size() == 3);
            CHECK(v[0] == 42);
            CHECK(v == test_bucket<int const>({42, 0, 0}));
            CHECK(v.data() == b.data());

            // Mutation of original is observable:
            b[1] = 123;
            CHECK(v[1] == 123);
        }
        CHECK(b == test_bucket<int>({42, 123, 0}));
    }

    SECTION("create view, destroy original first, view survives") {
        auto v = source->shared_view_of(b);
        b = {};
        CHECK(v == test_bucket<int const>({42, 0, 0}));
    }
}

TEST_CASE("recycling_bucket_source provides buckets up to max_count") {
    auto source = recycling_bucket_source<int>::create(2);
    CHECK_FALSE(source->supports_shared_views());
    auto b0 = source->bucket_of_size(3);
    {
        auto b1 = source->bucket_of_size(5);
        CHECK_THROWS_AS(source->bucket_of_size(7), buffer_overflow_error);
    }
    auto b1 = source->bucket_of_size(5);
    CHECK_THROWS_AS(source->bucket_of_size(7), buffer_overflow_error);
}

TEST_CASE("recycling_bucket_source clears recycled buckets iff requested") {
    auto non_clearing_source =
        recycling_bucket_source<int, false, false>::create(2);
    auto clearing_source =
        recycling_bucket_source<int, false, true>::create(2);

    {
        auto b0 = non_clearing_source->bucket_of_size(1);
        auto b1 = clearing_source->bucket_of_size(1);
        b0[0] = 42;
        b1[0] = 42;
    }
    auto b0 = non_clearing_source->bucket_of_size(1);
    auto b1 = clearing_source->bucket_of_size(1);
    CHECK(b0[0] == 42);
    CHECK(b1[0] == 0);
}

TEST_CASE(
    "blocking recycling_bucket_source provides buckets up to max_count") {
    auto source = recycling_bucket_source<int, true>::create(2);
    auto b0 = source->bucket_of_size(3);
    auto b1 = source->bucket_of_size(5);
    latch thread_start_latch(1);
    latch third_bucket_obtained_latch(1);
    std::thread t([&, source] {
        thread_start_latch.count_down();
        auto b = source->bucket_of_size(7);
        third_bucket_obtained_latch.count_down();
    });
    thread_start_latch.wait();
    wait_a_little(); // For thread to block waiting for bucket.
    b1 = {};
    third_bucket_obtained_latch.wait();
    t.join();
    auto bb = source->bucket_of_size(9);
}

TEST_CASE(
    "sharable_recycling_bucket_source provides buckets up to max_count") {
    auto source = sharable_recycling_bucket_source<int>::create(2);
    CHECK(source->supports_shared_views());
    auto b0 = source->bucket_of_size(3);
    {
        auto b1 = source->bucket_of_size(5);
        CHECK_THROWS_AS(source->bucket_of_size(7), buffer_overflow_error);
    }
    auto b1 = source->bucket_of_size(5);
    CHECK_THROWS_AS(source->bucket_of_size(7), buffer_overflow_error);
}

TEST_CASE(
    "sharable_recycling_bucket_source clears recycled buckets iff requested") {
    auto non_clearing_source =
        sharable_recycling_bucket_source<int, false, false>::create(2);
    auto clearing_source =
        sharable_recycling_bucket_source<int, false, true>::create(2);

    {
        auto b0 = non_clearing_source->bucket_of_size(1);
        auto b1 = clearing_source->bucket_of_size(1);
        b0[0] = 42;
        b1[0] = 42;
    }
    auto b0 = non_clearing_source->bucket_of_size(1);
    auto b1 = clearing_source->bucket_of_size(1);
    CHECK(b0[0] == 42);
    CHECK(b1[0] == 0);
}

TEST_CASE(
    "blocking sharable_recycling_bucket_source provides buckets up to max_count") {
    auto source = sharable_recycling_bucket_source<int, true>::create(2);
    auto b0 = source->bucket_of_size(3);
    auto b1 = source->bucket_of_size(5);
    latch thread_start_latch(1);
    latch third_bucket_obtained_latch(1);
    std::thread t([&, source] {
        thread_start_latch.count_down();
        auto b = source->bucket_of_size(7);
        third_bucket_obtained_latch.count_down();
    });
    thread_start_latch.wait();
    wait_a_little(); // For thread to block waiting for bucket.
    b1 = {};
    third_bucket_obtained_latch.wait();
    t.join();
    auto bb = source->bucket_of_size(9);
}

TEST_CASE("sharable_recycling_bucket_source provides sharable buckets") {
    auto source = sharable_recycling_bucket_source<int>::create();
    CHECK(source->supports_shared_views());
    auto b = source->bucket_of_size(3);
    CHECK(b.size() == 3);
    std::fill(b.begin(), b.end(), 0);
    b[0] = 42;

    SECTION("create view, destroy view, original survives") {
        {
            auto v = source->shared_view_of(b);
            static_assert(std::is_same_v<decltype(v), bucket<int const>>);
            CHECK(v.size() == 3);
            CHECK(v[0] == 42);
            CHECK(v == test_bucket<int const>({42, 0, 0}));
            CHECK(v.data() == b.data());

            // Mutation of original is observable:
            b[1] = 123;
            CHECK(v[1] == 123);
        }
        CHECK(b == test_bucket<int>({42, 123, 0}));
    }

    SECTION("create view, destroy original first, view survives") {
        auto v = source->shared_view_of(b);
        b = {};
        CHECK(v == test_bucket<int const>({42, 0, 0}));
    }
}

TEST_CASE(
    "sharable_recycling_bucket_source storage is recycled after all views discarded") {
    auto source = sharable_recycling_bucket_source<int>::create(2);
    CHECK(source->supports_shared_views());
    auto b0 = source->bucket_of_size(3);
    {
        auto b1 = source->bucket_of_size(5);
        auto v1 = source->shared_view_of(b1);
        b1 = {};
        CHECK_THROWS_AS(source->bucket_of_size(7), buffer_overflow_error);
    }
    auto b1 = source->bucket_of_size(5);
    auto v0 = source->shared_view_of(b0);
    b0 = {};
    CHECK_THROWS_AS(source->bucket_of_size(7), buffer_overflow_error);
    v0 = {};
    auto b2 = source->bucket_of_size(7);
}

namespace {

template <typename T> struct evt_with_bucket {
    bucket<T> data_bucket;
};

} // namespace

TEST_CASE("type constraints: extract_bucket") {
    using proc_type = decltype(extract_bucket<evt_with_bucket<int>>(
        sink_events<bucket<int>>()));
    STATIC_CHECK(is_processor_v<proc_type, evt_with_bucket<int>>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, evt_with_bucket<double>>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, bucket<int>>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("introspect: extract_bucket") {
    check_introspect_simple_processor(
        extract_bucket<evt_with_bucket<int>>(null_sink()));
}

TEST_CASE("extract_bucket preserves value category") {
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         extract_bucket<evt_with_bucket<int>>(
                             capture_output<type_list<bucket<int>>>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out =
        capture_output_checker<type_list<bucket<int>>>(valcat, ctx, "out");

    in.handle(evt_with_bucket<int>{test_bucket({42, 43})});
    CHECK(out.check(emitted_as::same_as_fed, test_bucket({42, 43})));
}

} // namespace tcspc
