/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/variant_event.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/event.hpp"
#include "libtcspc/type_list.hpp"

#include <catch2/catch_test_macros.hpp>

#include <ostream>
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
    ve const i0(42);
    ve const i1(42);
    ve const id(3.14);
    CHECK(i0 == i1);
    CHECK_FALSE(i0 != i1);
    CHECK_FALSE(i0 == id);
    CHECK(i0 != id);
}

namespace tests {

struct ostreamable_e {
    int x = 0;
    friend auto operator==(ostreamable_e const &, ostreamable_e const &)
        -> bool = default;
    friend auto operator<<(std::ostream &s, ostreamable_e const & /*unused*/)
        -> std::ostream & {
        return s << "ostreamable_e";
    }
};

struct silent_e {
    int x = 0;
    friend auto operator==(silent_e const &, silent_e const &)
        -> bool = default;
};

struct non_default_constructible {
    int x;
    explicit non_default_constructible(int v) : x(v) {}
};

} // namespace tests

TEST_CASE("variant_event conditional ostreamability") {
    using tests::ostreamable_e;
    using tests::silent_e;
    STATIC_CHECK(
        internal::ostreamable<variant_event<type_list<ostreamable_e>>>);

    STATIC_CHECK_FALSE(internal::ostreamable<
                       variant_event<type_list<ostreamable_e, silent_e>>>);
}

TEST_CASE("variant_event allows non-default-constructible element") {
    using ve = variant_event<type_list<tests::non_default_constructible>>;
    STATIC_CHECK(
        std::is_constructible_v<ve, tests::non_default_constructible>);
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
