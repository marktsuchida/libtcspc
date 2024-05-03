/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/variant_event.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/type_list.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <type_traits>
#include <variant>

namespace tcspc {

TEST_CASE("variant_event base class") {
    STATIC_CHECK(std::is_base_of_v<std::variant<int, long>,
                                   variant_event<type_list<int, long>>>);
}

TEST_CASE("variant_event stream insertion") {
    using ve = variant_event<type_list<int, double>>;
    ve const instance(42);
    std::ostringstream stream;
    stream << instance;
    CHECK(stream.str() == "42");
}

TEST_CASE("variant_event equality") {
    // Demonstrate that variant_event inherits members and operators from
    // std::variant.
    using ve = variant_event<type_list<int, double>>;
    ve const i0(int(42));
    ve const i1(int(42));
    ve const id(double(3.14));
    CHECK(i0 == i1);
    CHECK_FALSE(i0 != i1);
    CHECK_FALSE(i0 == id);
    CHECK(i0 != id);
}

TEST_CASE("variant_or_single_event") {
    STATIC_CHECK(std::is_same_v<variant_or_single_event<type_list<int>>, int>);
    STATIC_CHECK(
        std::is_same_v<variant_or_single_event<type_list<int, int>>, int>);
    STATIC_CHECK(std::is_same_v<variant_or_single_event<type_list<int, long>>,
                                variant_event<type_list<int, long>>>);
    STATIC_CHECK(
        std::is_same_v<variant_or_single_event<type_list<int, long, int>>,
                       variant_event<type_list<int, long>>>);

    // Test rvalue and lvalue refs for event parameter.

    int result{};
    visit_variant_or_single_event([&](auto const &e) { result = e; }, 42);
    CHECK(result == 42);
    int const i = 43;
    visit_variant_or_single_event([&](auto const &e) { result = e; }, i);
    CHECK(result == i);

    auto visitor = internal::overloaded{
        [&](int e) { result = e; },
        [&](double) { CHECK(false); },
    };
    visit_variant_or_single_event(visitor,
                                  variant_event<type_list<int, double>>(44));
    CHECK(result == 44);
    auto const evt = variant_event<type_list<int, double>>(45);
    visit_variant_or_single_event(visitor, evt);
    CHECK(result == 45);
}

} // namespace tcspc
