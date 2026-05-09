/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/processor_traits.hpp"

#include "libtcspc/type_list.hpp"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <type_traits>

namespace tcspc {

TEST_CASE("flushable") {
    struct p {
        void flush();
    };
    struct q {};
    struct r {
        void flush(int);
    };
    STATIC_CHECK(flushable<p>);
    STATIC_CHECK_FALSE(flushable<q>);
    STATIC_CHECK_FALSE(flushable<r>);
}

namespace {

struct e_rvalue {};
struct e_const_lvalue {};
struct e_both {};
struct e_by_value {};
struct e_forwarding_ref {};
struct e_not_handled {};

struct p {
    void handle(e_rvalue &&event);
    void handle(e_const_lvalue const &event);
    void handle(e_both &&event);
    void handle(e_both const &event);
    void handle(e_by_value event);

    template <typename E>
        requires std::convertible_to<std::remove_cvref_t<E>, e_forwarding_ref>
    void handle(E &&event);
};

struct q {
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    template <typename E> void handle(E && /* event */) {
        static_assert(
            std::is_convertible_v<std::remove_cvref_t<E>, e_forwarding_ref>);
    }
};

struct r {
    void handle(e_both &&event);
    void handle(e_both const &event);
    void flush();
};

} // namespace

TEST_CASE("rvalue_handler_for") {
    STATIC_CHECK(rvalue_handler_for<p, e_rvalue>);
    STATIC_CHECK(rvalue_handler_for<p, e_const_lvalue>);
    STATIC_CHECK(rvalue_handler_for<p, e_both>);
    STATIC_CHECK(rvalue_handler_for<p, e_by_value>);
    STATIC_CHECK(rvalue_handler_for<p, e_forwarding_ref>);
    STATIC_CHECK_FALSE(rvalue_handler_for<p, e_not_handled>);

    STATIC_CHECK(rvalue_handler_for<q, e_forwarding_ref>);
    // Cannot "see" static_assert failure (no ODR-use)
    STATIC_CHECK(rvalue_handler_for<q, e_not_handled>);
}

TEST_CASE("const_handler_for") {
    STATIC_CHECK_FALSE(const_handler_for<p, e_rvalue>);
    STATIC_CHECK(const_handler_for<p, e_const_lvalue>);
    STATIC_CHECK(const_handler_for<p, e_both>);
    STATIC_CHECK(const_handler_for<p, e_by_value>);
    STATIC_CHECK(const_handler_for<p, e_forwarding_ref>);
    STATIC_CHECK_FALSE(const_handler_for<p, e_not_handled>);

    STATIC_CHECK(const_handler_for<q, e_forwarding_ref>);
    // Cannot "see" static_assert failure (no ODR-use)
    STATIC_CHECK(const_handler_for<q, e_not_handled>);
}

TEST_CASE("handler_for") {
    STATIC_CHECK_FALSE(handler_for<p, e_rvalue>);
    STATIC_CHECK(handler_for<p, e_const_lvalue>);
    STATIC_CHECK(handler_for<p, e_both>);
    STATIC_CHECK(handler_for<p, e_by_value>);
    STATIC_CHECK(handler_for<p, e_forwarding_ref>);
    STATIC_CHECK_FALSE(handler_for<p, e_not_handled>);

    STATIC_CHECK(handler_for<q, e_forwarding_ref>);
    // Cannot "see" static_assert failure (no ODR-use)
    STATIC_CHECK(handler_for<q, e_not_handled>);
}

TEST_CASE("handler_for variadic") {
    STATIC_CHECK(handler_for<p, e_const_lvalue, e_both, e_by_value>);
    STATIC_CHECK_FALSE(handler_for<p, e_rvalue, e_both, e_by_value>);
    STATIC_CHECK(not handler_for<p, e_const_lvalue, e_both, e_not_handled>);
}

TEST_CASE("handles_event_list") {
    STATIC_CHECK(
        handles_event_list_v<p,
                             type_list<e_const_lvalue, e_both, e_by_value>>);
    STATIC_CHECK_FALSE(
        handles_event_list_v<p, type_list<e_rvalue, e_both, e_by_value>>);
    STATIC_CHECK_FALSE(handles_event_list_v<
                       p, type_list<e_const_lvalue, e_both, e_not_handled>>);
}

TEST_CASE("processor") {
    STATIC_CHECK_FALSE(processor<p>);
    STATIC_CHECK_FALSE(processor<p, e_both>);
    STATIC_CHECK(processor<r>);
    STATIC_CHECK(processor<r, e_both>);
    STATIC_CHECK_FALSE(processor<r, e_both, e_const_lvalue>);
}

TEST_CASE("is_processor_of_list") {
    STATIC_CHECK_FALSE(is_processor_of_list_v<p, type_list<>>);
    STATIC_CHECK_FALSE(is_processor_of_list_v<p, type_list<e_both>>);
    STATIC_CHECK(is_processor_of_list_v<r, type_list<>>);
    STATIC_CHECK(is_processor_of_list_v<r, type_list<e_both>>);
    STATIC_CHECK_FALSE(
        is_processor_of_list_v<r, type_list<e_both, e_const_lvalue>>);
}

} // namespace tcspc
