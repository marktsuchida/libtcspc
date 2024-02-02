/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/vector_queue.hpp"

#include <catch2/catch_all.hpp>

#include <memory>
#include <utility>

namespace tcspc::internal {

// Tests assume first allocation capacity is 3 elements.

TEST_CASE("Vector queue of int") {
    vector_queue<int> q;

    // NOLINTBEGIN(readability-container-size-empty)

    REQUIRE(q.empty());
    REQUIRE(q.size() == 0);
    q.for_each([](int) { REQUIRE(false); });

    auto p = q;
    REQUIRE(p.empty());
    REQUIRE(p.size() == 0);

    auto r = std::move(p);
    REQUIRE(r.empty());
    REQUIRE(r.size() == 0);
    // Moved-out should be valid and empty
    // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    REQUIRE(p.empty());
    REQUIRE(p.size() == 0);
    // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

    q.push(42);
    REQUIRE_FALSE(q.empty());
    REQUIRE(q.size() == 1);
    REQUIRE(q.front() == 42);
    REQUIRE(q.back() == 42);
    q.for_each([](int x) { REQUIRE(x == 42); });
    p = q;
    REQUIRE(p.size() == 1);
    r = std::move(p);
    REQUIRE(r.size() == 1);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    REQUIRE(p.size() == 0);
    r.pop();
    REQUIRE(r.empty());

    // NOLINTEND(readability-container-size-empty)

    p = q;
    r.swap(p);
    REQUIRE_FALSE(r.empty());
    REQUIRE(r.size() == 1);
    REQUIRE(r.front() == 42);

    q.push(43); // (42, 43, -)
    REQUIRE_FALSE(q.empty());
    REQUIRE(q.size() == 2);
    REQUIRE(q.front() == 42);
    REQUIRE(q.back() == 43);
    q.pop(); // (-, 43, -)
    REQUIRE_FALSE(q.empty());
    REQUIRE(q.size() == 1);
    REQUIRE(q.front() == 43);
    REQUIRE(q.back() == 43);
    q.push(44); // (-, 43, 44)
    REQUIRE_FALSE(q.empty());
    REQUIRE(q.size() == 2);
    REQUIRE(q.front() == 43);
    REQUIRE(q.back() == 44);
    int i = 0;
    q.for_each([&i](int x) mutable {
        REQUIRE(i < 2);
        if (i == 0)
            REQUIRE(x == 43);
        else
            REQUIRE(x == 44);
        ++i;
    });

    q.pop();    // (-, -, 44)
    q.push(45); // (45, -, 44)
    REQUIRE(q.size() == 2);
    REQUIRE(q.front() == 44);
    REQUIRE(q.back() == 45);
    i = 0;
    q.for_each([&i](int x) mutable {
        REQUIRE(i < 2);
        if (i == 0)
            REQUIRE(x == 44);
        else
            REQUIRE(x == 45);
        ++i;
    });

    // Copy & move discontiguous (45, -, 44)
    p = q;
    REQUIRE(p.size() == 2);
    REQUIRE(p.front() == 44);
    REQUIRE(p.back() == 45);
    r = std::move(p);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    REQUIRE(p.empty());
    REQUIRE(r.size() == 2);
    REQUIRE(r.front() == 44);
    REQUIRE(r.back() == 45);

    q.pop(); // (45, -, -)
    REQUIRE(q.front() == 45);
    REQUIRE(q.back() == 45);
    q.pop(); // (-, -, -)
    REQUIRE(q.empty());
}

TEST_CASE("Vector queue of shared_ptr") {
    vector_queue<std::shared_ptr<int>> q;

    auto p = q;
    auto r = std::move(p);
    q.push(std::make_shared<int>(42));
    REQUIRE(*q.front() == 42);
    p = q;
    REQUIRE(*p.front() == 42);
    r = std::move(p);
    REQUIRE(*r.front() == 42);
    r.pop();
    REQUIRE(r.empty());

    p = q;
    r.swap(p);
    REQUIRE(*r.front() == 42);

    q.push(std::make_shared<int>(43)); // (42, 43, -)
    REQUIRE(*q.front() == 42);
    REQUIRE(*q.back() == 43);
    q.pop(); // (-, 43, -)
    REQUIRE(*q.front() == 43);
    REQUIRE(*q.back() == 43);
    q.push(std::make_shared<int>(44)); // (-, 43, 44)
    REQUIRE(*q.front() == 43);
    REQUIRE(*q.back() == 44);

    q.pop();                           // (-, -, 44)
    q.push(std::make_shared<int>(45)); // (45, -, 44)
    REQUIRE(*q.front() == 44);
    REQUIRE(*q.back() == 45);

    // Copy & move discontiguous (45, -, 44)
    p = q;
    REQUIRE(*p.front() == 44);
    REQUIRE(*p.back() == 45);
    r = std::move(p);
    REQUIRE(*r.front() == 44);
    REQUIRE(*r.back() == 45);
}

} // namespace tcspc::internal
