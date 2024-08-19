/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/move_only_any.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <typeinfo>
#include <utility>
#include <vector>

namespace tcspc::internal {

TEST_CASE("empty move_only_any behaves as expected") {
    move_only_any a;

    SECTION("has no value") { CHECK_FALSE(a.has_value()); }

    SECTION("has type void") { CHECK(a.type() == typeid(void)); }

    SECTION("can move") {
        auto b = std::move(a);
        CHECK_FALSE(b.has_value());
        a = std::move(b);
        CHECK_FALSE(a.has_value());
    }

    SECTION("cast to int indicates error") {
        CHECK(move_only_any_cast<int>(&a) == nullptr);
        CHECK(move_only_any_cast<int const>(&a) == nullptr);

        CHECK_THROWS_AS(move_only_any_cast<int const &>(std::as_const(a)),
                        bad_move_only_any_cast);

        CHECK_THROWS_AS(move_only_any_cast<int &>(a), bad_move_only_any_cast);
        CHECK_THROWS_AS(move_only_any_cast<int const &>(a),
                        bad_move_only_any_cast);

        CHECK_THROWS_AS(move_only_any_cast<int>(std::move(a)),
                        bad_move_only_any_cast);
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        CHECK_THROWS_AS(move_only_any_cast<int const &>(std::move(a)),
                        bad_move_only_any_cast);
    }
}

TEST_CASE("move_only_any sbo") {
    move_only_any a = std::make_unique<int>(42);

    SECTION("has value") { CHECK(a.has_value()); }

    SECTION("has correct type") {
        CHECK(a.type() == typeid(std::unique_ptr<int>));
    }

    SECTION("can move construct") {
        move_only_any b = std::move(a);
        CHECK(b.has_value());
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        CHECK_FALSE(a.has_value());
        CHECK(*move_only_any_cast<std::unique_ptr<int> const &>(b) == 42);
    }

    SECTION("can move assign") {
        move_only_any b;
        b = std::move(a);
        CHECK(b.has_value());
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        CHECK_FALSE(a.has_value());
        CHECK(*move_only_any_cast<std::unique_ptr<int> const &>(b) == 42);
    }
}

TEST_CASE("move_only_any non-sbo") {
    struct non_copyable_large {
        std::unique_ptr<int> p;
        std::array<int, 100> a;
    };
    move_only_any a = non_copyable_large{{}, {42, 43}};

    SECTION("has value") { CHECK(a.has_value()); }

    SECTION("has correct type") {
        CHECK(a.type() == typeid(non_copyable_large));
    }

    SECTION("can move construct") {
        move_only_any b = std::move(a);
        CHECK(b.has_value());
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        CHECK_FALSE(a.has_value());
        auto const &v = move_only_any_cast<non_copyable_large const &>(b);
        CHECK(v.a[0] == 42);
    }

    SECTION("can move assign") {
        move_only_any b;
        b = std::move(a);
        CHECK(b.has_value());
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        CHECK_FALSE(a.has_value());
        auto const &v = move_only_any_cast<non_copyable_large const &>(b);
        CHECK(v.a[0] == 42);
    }
}

TEST_CASE(
    "move_only_any construct or assign from lvalue ref of copyable type") {
    SECTION("sbo") {
        int const v = 42;
        move_only_any a(v);
        CHECK(move_only_any_cast<int const &>(a) == 42);
        int const v2 = 43;
        a = v2;
        CHECK(move_only_any_cast<int const &>(a) == 43);
    }

    SECTION("heap") {
        using i100 = std::array<int, 100>;
        i100 const v{42, 43};
        move_only_any a(v);
        CHECK(move_only_any_cast<i100 const &>(a) == i100{42, 43});
        i100 const v2{44, 45};
        a = v2;
        CHECK(move_only_any_cast<i100 const &>(a) == i100{44, 45});
    }
}

namespace {

// Example type for testing functions taking std::initializer_list and variadic
// args in non-sbo (heap) mode. We use the std::vector constructor taking an
// allocator as an example. (std::pmr::vector would have been a good example
// but Apple did not support it until Xcode 15 and macOS 14.)

// std::vector<V, my_allocator<V>> fits in sbo.
template <typename T> struct my_allocator {
    using value_type = T;

    my_allocator() = default;

    // NOLINTBEGIN(google-explicit-constructor,cppcoreguidelines-rvalue-reference-param-not-moved)

    // For testing arg passing via std::vector constructor.
    my_allocator(int /* i */) {}

    template <class U> my_allocator(my_allocator<U> const & /* other */) {}

    template <class U>
    auto operator=(my_allocator<U> const & /* rhs */) -> my_allocator & {
        return *this;
    }

    template <class U> my_allocator(my_allocator<U> && /* other */) {}

    template <class U>
    auto operator=(my_allocator<U> && /* rhs */) -> my_allocator & {
        return *this;
    }

    // NOLINTEND(google-explicit-constructor,cppcoreguidelines-rvalue-reference-param-not-moved)

    // NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)

    auto allocate(std::size_t n) -> T * {
        if (n <= std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            if (void *ptr = std::malloc(n * sizeof(T)))
                return static_cast<T *>(ptr);
        }
        throw std::bad_alloc();
    }

    void deallocate(T *ptr, std::size_t /* n */) { std::free(ptr); }

    // NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc)

    template <typename U>
    friend auto operator==(my_allocator<T> const & /* lhs */,
                           my_allocator<U> const & /* rhs */) -> bool {
        return true;
    }

    template <typename U>
    friend auto operator!=(my_allocator<T> const &lhs,
                           my_allocator<U> const &rhs) -> bool {
        return !(lhs == rhs);
    }
};

#ifndef _MSC_VER
// MSVC STL std::vector is 4x sizeof(void *) when a custom allocator is used,
// even if empty/trivial. So we skip testing the SBO case, but the tests still
// work using the heap.

static_assert(sizeof(std::vector<int, my_allocator<int>>) <=
                  move_only_any_sbo_size,
              "must fit in sbo");

#endif

// large_value does not fit in sbo.
template <typename V> struct large_value {
    std::vector<V, my_allocator<V>> v;
    std::array<int, 100> a{}; // Don't fit in sbo.

    template <typename T, typename... Args>
    large_value(std::initializer_list<T> il, Args &&...args)
        : v(il, std::forward<Args>(args)...) {}
};

} // namespace

TEST_CASE("move_only_any in-place construct") {
    SECTION("variadic args") {
        SECTION("sbo") {
            move_only_any a(std::in_place_type<std::vector<int>>,
                            std::size_t(3), 42);
            CHECK(move_only_any_cast<std::vector<int> const &>(a) ==
                  std::vector<int>{42, 42, 42});
        }

        SECTION("heap") {
            using i100 = std::array<int, 100>;
            move_only_any a(std::in_place_type<i100>);
            CHECK(move_only_any_cast<i100 const &>(a) == i100{});
        }
    }

    SECTION("initializer_list and args") {
        SECTION("sbo") {
            using myvec = std::vector<int, my_allocator<int>>;
            move_only_any a(std::in_place_type<myvec>, {42, 43, 44}, 128);
            CHECK(move_only_any_cast<myvec const &>(a).size() == 3);
        }

        SECTION("heap") {
            move_only_any a(std::in_place_type<large_value<int>>, {42, 43, 44},
                            128);
            CHECK(move_only_any_cast<large_value<int> const &>(a).v.size() ==
                  3);
        }
    }
}

TEST_CASE("move_only_any emplace") {
    SECTION("variadic args") {
        SECTION("sbo") {
            move_only_any a;
            a.emplace<std::vector<int>>(std::size_t(3), 42);
            CHECK(move_only_any_cast<std::vector<int> const &>(a) ==
                  std::vector<int>{42, 42, 42});
        }

        SECTION("heap") {
            using i100 = std::array<int, 100>;
            move_only_any a;
            a.emplace<i100>();
            CHECK(move_only_any_cast<i100 const &>(a) == i100{});
        }
    }

    SECTION("initializer_list and args") {
        SECTION("sbo") {
            move_only_any a;
            using myvec = std::vector<int, my_allocator<int>>;
            a.emplace<myvec>({42, 43, 44}, 128);
            CHECK(move_only_any_cast<myvec const &>(a).size() == 3);
        }

        SECTION("heap") {
            move_only_any a;
            a.emplace<large_value<int>>({42, 43, 44}, 128);
            CHECK(move_only_any_cast<large_value<int> const &>(a).v.size() ==
                  3);
        }
    }
}

} // namespace tcspc::internal
