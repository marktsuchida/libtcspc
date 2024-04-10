/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/bucket.hpp"

#include "libtcspc/span.hpp"
#include "test_thread_utils.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <sstream>
#include <thread>
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

    CHECK(b.subbucket(0, 0).empty());
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

TEST_CASE("subbucket shares observable storage") {
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), std::move(v));
    auto bb = b.subbucket(1, 1);
    CHECK(bb.size() == 1);
    CHECK(bb[0] == 43);
    CHECK(&bb.storage<std::vector<int>>() == &b.storage<std::vector<int>>());
}

TEST_CASE("unrelated buckets compare equal if data equal") {
    std::vector<int> v{42, 43, 44};
    auto b = bucket<int>(span(v), std::move(v));
    std::array<int, 3> a{42, 43, 44};
    auto bb = bucket<int>(span(a), nullptr);
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
    auto b = source->bucket_of_size(3);
    CHECK(b.size() == 3);
    b[0] = 42;
    b[2] = 44;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    auto e = b.extract_storage<std::unique_ptr<int[]>>();
    CHECK(e[0] == 42);
    CHECK(e[2] == 44);
}

TEST_CASE("recycling_bucket_source provides buckets up to max_count") {
    auto source = recycling_bucket_source<int>::create(2);
    auto b0 = source->bucket_of_size(3);
    {
        auto b1 = source->bucket_of_size(5);
        CHECK_THROWS(source->bucket_of_size(7));
    }
    auto b1 = source->bucket_of_size(5);
    CHECK_THROWS(source->bucket_of_size(7));
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
    { auto const discard = std::move(b1); }
    third_bucket_obtained_latch.wait();
    t.join();
    auto bb = source->bucket_of_size(9);
}

} // namespace tcspc
