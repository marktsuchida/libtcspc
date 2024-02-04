/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/type_list.hpp"

#include <catch2/catch_all.hpp>

#include <type_traits>

namespace tcspc {

TEST_CASE("is_type_list") {
    static_assert(is_type_list_v<type_list<>>);
    static_assert(is_type_list_v<type_list<int>>);
    static_assert(is_type_list_v<type_list<int, long>>);
    static_assert(not is_type_list_v<int>);
}

TEST_CASE("type_list_size") {
    static_assert(type_list_size_v<type_list<>> == 0);
    static_assert(type_list_size_v<type_list<int>> == 1);
    static_assert(type_list_size_v<type_list<int, int>> == 2);
    static_assert(type_list_size_v<type_list<int, long>> == 2);
}

TEST_CASE("type_list_singleton_element") {
    static_assert(
        std::is_same_v<type_list_singleton_element_t<type_list<int>>, int>);
}

TEST_CASE("type_list_contains") {
    static_assert(not type_list_contains_v<type_list<>, int>);
    static_assert(type_list_contains_v<type_list<int>, int>);
    static_assert(not type_list_contains_v<type_list<int>, long>);
    static_assert(type_list_contains_v<type_list<int, long>, int>);
    static_assert(type_list_contains_v<type_list<int, long>, long>);
    static_assert(not type_list_contains_v<type_list<int, long>, double>);
}

TEST_CASE("type_list_is_subset") {
    static_assert(type_list_is_subset_v<type_list<>, type_list<>>);
    static_assert(not type_list_is_subset_v<type_list<int>, type_list<>>);
    static_assert(type_list_is_subset_v<type_list<>, type_list<int>>);
    static_assert(type_list_is_subset_v<type_list<int>, type_list<int>>);
    static_assert(not type_list_is_subset_v<type_list<long>, type_list<int>>);
    static_assert(type_list_is_subset_v<type_list<int>, type_list<int, long>>);
    static_assert(
        type_list_is_subset_v<type_list<long>, type_list<int, long>>);
    static_assert(
        not type_list_is_subset_v<type_list<double>, type_list<int, long>>);
}

TEST_CASE("type_list_is_equal_set") {
    static_assert(type_list_is_equal_set_v<type_list<>, type_list<>>);
    static_assert(not type_list_is_equal_set_v<type_list<int>, type_list<>>);
    static_assert(not type_list_is_equal_set_v<type_list<>, type_list<int>>);
    static_assert(type_list_is_equal_set_v<type_list<int>, type_list<int>>);
    static_assert(
        not type_list_is_equal_set_v<type_list<long>, type_list<int>>);
    static_assert(
        not type_list_is_equal_set_v<type_list<int>, type_list<int, long>>);
    static_assert(
        type_list_is_equal_set_v<type_list<long, int>, type_list<int, long>>);
    static_assert(type_list_is_equal_set_v<type_list<long, int>,
                                           type_list<int, long, int>>);
    static_assert(
        not type_list_is_equal_set_v<type_list<double>, type_list<int, long>>);
}

TEST_CASE("unique_type_list") {
    static_assert(
        std::is_same_v<unique_type_list_t<type_list<>>, type_list<>>);
    static_assert(
        std::is_same_v<unique_type_list_t<type_list<int>>, type_list<int>>);
    static_assert(std::is_same_v<unique_type_list_t<type_list<int, int>>,
                                 type_list<int>>);
    static_assert(std::is_same_v<unique_type_list_t<type_list<int, int, int>>,
                                 type_list<int>>);
    static_assert(std::is_same_v<unique_type_list_t<type_list<long, int, int>>,
                                 type_list<long, int>>);
    static_assert(std::is_same_v<unique_type_list_t<type_list<int, long, int>>,
                                 type_list<int, long>>);
    static_assert(std::is_same_v<unique_type_list_t<type_list<int, int, long>>,
                                 type_list<int, long>>);
}

TEST_CASE("type_list_union") {
    static_assert(std::is_same_v<type_list_union_t<type_list<>, type_list<>>,
                                 type_list<>>);
    static_assert(
        std::is_same_v<type_list_union_t<type_list<int>, type_list<>>,
                       type_list<int>>);
    static_assert(
        std::is_same_v<type_list_union_t<type_list<>, type_list<int>>,
                       type_list<int>>);
    static_assert(
        std::is_same_v<type_list_union_t<type_list<int>, type_list<int>>,
                       type_list<int>>);
    static_assert(
        std::is_same_v<type_list_union_t<type_list<int>, type_list<long>>,
                       type_list<int, long>>);
    static_assert(
        std::is_same_v<
            type_list_union_t<type_list<int, long>, type_list<long, int>>,
            type_list<int, long>>);
    static_assert(
        std::is_same_v<type_list_union_t<type_list<int, long, double>,
                                         type_list<long, double, float>>,
                       type_list<int, long, double, float>>);
}

TEST_CASE("type_list_intersection") {
    static_assert(
        std::is_same_v<type_list_intersection_t<type_list<>, type_list<>>,
                       type_list<>>);
    static_assert(
        std::is_same_v<type_list_intersection_t<type_list<int>, type_list<>>,
                       type_list<>>);
    static_assert(
        std::is_same_v<type_list_intersection_t<type_list<>, type_list<int>>,
                       type_list<>>);
    static_assert(std::is_same_v<
                  type_list_intersection_t<type_list<int>, type_list<int>>,
                  type_list<int>>);
    static_assert(std::is_same_v<
                  type_list_intersection_t<type_list<int>, type_list<long>>,
                  type_list<>>);
    static_assert(
        std::is_same_v<type_list_intersection_t<type_list<int, long>,
                                                type_list<long, int>>,
                       type_list<int, long>>);
    static_assert(std::is_same_v<
                  type_list_intersection_t<type_list<int, long, double>,
                                           type_list<long, double, float>>,
                  type_list<long, double>>);
}

TEST_CASE("type_list_set_difference") {
    static_assert(
        std::is_same_v<type_list_set_difference_t<type_list<>, type_list<>>,
                       type_list<>>);
    static_assert(
        std::is_same_v<type_list_set_difference_t<type_list<int>, type_list<>>,
                       type_list<int>>);
    static_assert(
        std::is_same_v<type_list_set_difference_t<type_list<>, type_list<int>>,
                       type_list<>>);
    static_assert(std::is_same_v<
                  type_list_set_difference_t<type_list<int>, type_list<int>>,
                  type_list<>>);
    static_assert(std::is_same_v<
                  type_list_set_difference_t<type_list<int>, type_list<long>>,
                  type_list<int>>);
    static_assert(
        std::is_same_v<
            type_list_set_difference_t<type_list<int, long>, type_list<long>>,
            type_list<int>>);
    static_assert(
        std::is_same_v<type_list_set_difference_t<type_list<int, long>,
                                                  type_list<long, int>>,
                       type_list<>>);
    static_assert(std::is_same_v<
                  type_list_set_difference_t<type_list<int, long, double>,
                                             type_list<long, double, float>>,
                  type_list<int>>);
}

} // namespace tcspc
