/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/type_list.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace tcspc {

TEST_CASE("type_list_like") {
    STATIC_CHECK(type_list_like<type_list<>>);
    STATIC_CHECK(type_list_like<type_list<int>>);
    STATIC_CHECK(type_list_like<type_list<int, long>>);
    STATIC_CHECK_FALSE(type_list_like<int>);
}

namespace tests {

struct comparable {
    int x = 0;
    friend auto operator==(comparable const &, comparable const &)
        -> bool = default;
};

struct move_only {
    move_only() = default;
    ~move_only() = default;
    move_only(move_only const &) = delete;
    auto operator=(move_only const &) -> move_only & = delete;
    move_only(move_only &&) = default;
    auto operator=(move_only &&) -> move_only & = default;
};

struct no_move_assign {
    no_move_assign() = default;
    ~no_move_assign() = default;
    no_move_assign(no_move_assign const &) = default;
    auto operator=(no_move_assign const &) -> no_move_assign & = delete;
    no_move_assign(no_move_assign &&) = default;
    auto operator=(no_move_assign &&) -> no_move_assign & = delete;
};

} // namespace tests

TEST_CASE("type-list trait predicates") {
    using internal::is_copy_constructible_list_v;
    using internal::is_equality_comparable_list_v;
    using internal::is_move_assignable_list_v;
    using internal::is_move_constructible_list_v;
    using tests::comparable;
    using tests::move_only;
    using tests::no_move_assign;

    // Non-type_list arguments yield false.
    STATIC_CHECK_FALSE(is_copy_constructible_list_v<int>);
    STATIC_CHECK_FALSE(is_move_constructible_list_v<int>);

    STATIC_CHECK(is_copy_constructible_list_v<type_list<>>);
    STATIC_CHECK(is_copy_constructible_list_v<type_list<comparable, int>>);
    STATIC_CHECK_FALSE(is_copy_constructible_list_v<type_list<move_only>>);
    STATIC_CHECK_FALSE(
        is_copy_constructible_list_v<type_list<comparable, move_only>>);

    STATIC_CHECK(
        is_move_constructible_list_v<type_list<comparable, move_only>>);

    STATIC_CHECK(is_move_assignable_list_v<type_list<comparable, move_only>>);
    STATIC_CHECK_FALSE(is_move_assignable_list_v<type_list<no_move_assign>>);

    STATIC_CHECK(is_equality_comparable_list_v<type_list<comparable>>);
    STATIC_CHECK_FALSE(is_equality_comparable_list_v<type_list<move_only>>);
    STATIC_CHECK_FALSE(
        is_equality_comparable_list_v<type_list<comparable, move_only>>);
}

TEST_CASE("type_list_size") {
    STATIC_CHECK(type_list_size_v<type_list<>> == 0);
    STATIC_CHECK(type_list_size_v<type_list<int>> == 1);
    STATIC_CHECK(type_list_size_v<type_list<int, int>> == 2);
    STATIC_CHECK(type_list_size_v<type_list<int, long>> == 2);
}

TEST_CASE("type_list_singleton_element") {
    STATIC_CHECK(
        std::is_same_v<type_list_singleton_element_t<type_list<int>>, int>);
}

TEST_CASE("type_list_member") {
    STATIC_CHECK_FALSE(type_list_member<int, type_list<>>);
    STATIC_CHECK(type_list_member<int, type_list<int>>);
    STATIC_CHECK_FALSE(type_list_member<long, type_list<int>>);
    STATIC_CHECK(type_list_member<int, type_list<int, long>>);
    STATIC_CHECK(type_list_member<long, type_list<int, long>>);
    STATIC_CHECK_FALSE(type_list_member<double, type_list<int, long>>);
}

TEST_CASE("convertible_to_type_list_member") {
    STATIC_CHECK_FALSE(convertible_to_type_list_member<short, type_list<>>);
    STATIC_CHECK(convertible_to_type_list_member<short, type_list<short>>);
    STATIC_CHECK(convertible_to_type_list_member<short, type_list<long>>);
    struct some_type {};
    STATIC_CHECK_FALSE(
        convertible_to_type_list_member<short, type_list<some_type>>);
    STATIC_CHECK(
        convertible_to_type_list_member<short, type_list<some_type, long>>);
}

TEST_CASE("type_list_is_subset") {
    STATIC_CHECK(type_list_is_subset_v<type_list<>, type_list<>>);
    STATIC_CHECK_FALSE(type_list_is_subset_v<type_list<int>, type_list<>>);
    STATIC_CHECK(type_list_is_subset_v<type_list<>, type_list<int>>);
    STATIC_CHECK(type_list_is_subset_v<type_list<int>, type_list<int>>);
    STATIC_CHECK_FALSE(type_list_is_subset_v<type_list<long>, type_list<int>>);
    STATIC_CHECK(type_list_is_subset_v<type_list<int>, type_list<int, long>>);
    STATIC_CHECK(type_list_is_subset_v<type_list<long>, type_list<int, long>>);
    STATIC_CHECK_FALSE(
        type_list_is_subset_v<type_list<double>, type_list<int, long>>);
}

TEST_CASE("type_list_is_equal_set") {
    STATIC_CHECK(type_list_is_equal_set_v<type_list<>, type_list<>>);
    STATIC_CHECK_FALSE(type_list_is_equal_set_v<type_list<int>, type_list<>>);
    STATIC_CHECK_FALSE(type_list_is_equal_set_v<type_list<>, type_list<int>>);
    STATIC_CHECK(type_list_is_equal_set_v<type_list<int>, type_list<int>>);
    STATIC_CHECK_FALSE(
        type_list_is_equal_set_v<type_list<long>, type_list<int>>);
    STATIC_CHECK_FALSE(
        type_list_is_equal_set_v<type_list<int>, type_list<int, long>>);
    STATIC_CHECK(
        type_list_is_equal_set_v<type_list<long, int>, type_list<int, long>>);
    STATIC_CHECK(type_list_is_equal_set_v<type_list<long, int>,
                                          type_list<int, long, int>>);
    STATIC_CHECK_FALSE(
        type_list_is_equal_set_v<type_list<double>, type_list<int, long>>);
}

TEST_CASE("unique_type_list") {
    STATIC_CHECK(std::is_same_v<unique_type_list_t<type_list<>>, type_list<>>);
    STATIC_CHECK(
        std::is_same_v<unique_type_list_t<type_list<int>>, type_list<int>>);
    STATIC_CHECK(std::is_same_v<unique_type_list_t<type_list<int, int>>,
                                type_list<int>>);
    STATIC_CHECK(std::is_same_v<unique_type_list_t<type_list<int, int, int>>,
                                type_list<int>>);
    STATIC_CHECK(std::is_same_v<unique_type_list_t<type_list<long, int, int>>,
                                type_list<long, int>>);
    STATIC_CHECK(std::is_same_v<unique_type_list_t<type_list<int, long, int>>,
                                type_list<int, long>>);
    STATIC_CHECK(std::is_same_v<unique_type_list_t<type_list<int, int, long>>,
                                type_list<int, long>>);
}

TEST_CASE("type_list_union") {
    STATIC_CHECK(std::is_same_v<type_list_union_t<type_list<>, type_list<>>,
                                type_list<>>);
    STATIC_CHECK(std::is_same_v<type_list_union_t<type_list<int>, type_list<>>,
                                type_list<int>>);
    STATIC_CHECK(std::is_same_v<type_list_union_t<type_list<>, type_list<int>>,
                                type_list<int>>);
    STATIC_CHECK(
        std::is_same_v<type_list_union_t<type_list<int>, type_list<int>>,
                       type_list<int>>);
    STATIC_CHECK(
        std::is_same_v<type_list_union_t<type_list<int>, type_list<long>>,
                       type_list<int, long>>);
    STATIC_CHECK(std::is_same_v<
                 type_list_union_t<type_list<int, long>, type_list<long, int>>,
                 type_list<int, long>>);
    STATIC_CHECK(
        std::is_same_v<type_list_union_t<type_list<int, long, double>,
                                         type_list<long, double, float>>,
                       type_list<int, long, double, float>>);
}

TEST_CASE("type_list_intersection") {
    STATIC_CHECK(
        std::is_same_v<type_list_intersection_t<type_list<>, type_list<>>,
                       type_list<>>);
    STATIC_CHECK(
        std::is_same_v<type_list_intersection_t<type_list<int>, type_list<>>,
                       type_list<>>);
    STATIC_CHECK(
        std::is_same_v<type_list_intersection_t<type_list<>, type_list<int>>,
                       type_list<>>);
    STATIC_CHECK(std::is_same_v<
                 type_list_intersection_t<type_list<int>, type_list<int>>,
                 type_list<int>>);
    STATIC_CHECK(std::is_same_v<
                 type_list_intersection_t<type_list<int>, type_list<long>>,
                 type_list<>>);
    STATIC_CHECK(std::is_same_v<type_list_intersection_t<type_list<int, long>,
                                                         type_list<long, int>>,
                                type_list<int, long>>);
    STATIC_CHECK(std::is_same_v<
                 type_list_intersection_t<type_list<int, long, double>,
                                          type_list<long, double, float>>,
                 type_list<long, double>>);
}

TEST_CASE("type_list_set_difference") {
    STATIC_CHECK(
        std::is_same_v<type_list_set_difference_t<type_list<>, type_list<>>,
                       type_list<>>);
    STATIC_CHECK(
        std::is_same_v<type_list_set_difference_t<type_list<int>, type_list<>>,
                       type_list<int>>);
    STATIC_CHECK(
        std::is_same_v<type_list_set_difference_t<type_list<>, type_list<int>>,
                       type_list<>>);
    STATIC_CHECK(std::is_same_v<
                 type_list_set_difference_t<type_list<int>, type_list<int>>,
                 type_list<>>);
    STATIC_CHECK(std::is_same_v<
                 type_list_set_difference_t<type_list<int>, type_list<long>>,
                 type_list<int>>);
    STATIC_CHECK(
        std::is_same_v<
            type_list_set_difference_t<type_list<int, long>, type_list<long>>,
            type_list<int>>);
    STATIC_CHECK(
        std::is_same_v<type_list_set_difference_t<type_list<int, long>,
                                                  type_list<long, int>>,
                       type_list<>>);
    STATIC_CHECK(std::is_same_v<
                 type_list_set_difference_t<type_list<int, long, double>,
                                            type_list<long, double, float>>,
                 type_list<int>>);
}

} // namespace tcspc
