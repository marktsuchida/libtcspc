/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/processor_traits.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/type_list.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace tcspc {

TEST_CASE("handles_flush") {
    struct p {
        void flush();
    };
    struct q {};
    struct r {
        void flush(int);
    };
    static_assert(handles_flush_v<p>);
    static_assert(not handles_flush_v<q>);
    static_assert(not handles_flush_v<r>);
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

    template <typename E, typename = std::enable_if_t<std::is_convertible_v<
                              internal::remove_cvref_t<E>, e_forwarding_ref>>>
    void handle(E &&event);
};

struct q {
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    template <typename E> void handle([[maybe_unused]] E &&event) {
        static_assert(std::is_convertible_v<internal::remove_cvref_t<E>,
                                            e_forwarding_ref>);
    }
};

} // namespace

TEST_CASE("handles_rvalue_event") {
    static_assert(handles_rvalue_event_v<p, e_rvalue>);
    static_assert(handles_rvalue_event_v<p, e_const_lvalue>);
    static_assert(handles_rvalue_event_v<p, e_both>);
    static_assert(handles_rvalue_event_v<p, e_by_value>);
    static_assert(handles_rvalue_event_v<p, e_forwarding_ref>);
    static_assert(not handles_rvalue_event_v<p, e_not_handled>);

    static_assert(handles_rvalue_event_v<q, e_forwarding_ref>);
    // Cannot "see" static_assert failure (no ODR-use)
    static_assert(handles_rvalue_event_v<q, e_not_handled>);
}

TEST_CASE("handles_const_event") {
    static_assert(not handles_const_event_v<p, e_rvalue>);
    static_assert(handles_const_event_v<p, e_const_lvalue>);
    static_assert(handles_const_event_v<p, e_both>);
    static_assert(handles_const_event_v<p, e_by_value>);
    static_assert(handles_const_event_v<p, e_forwarding_ref>);
    static_assert(not handles_const_event_v<p, e_not_handled>);

    static_assert(handles_const_event_v<q, e_forwarding_ref>);
    // Cannot "see" static_assert failure (no ODR-use)
    static_assert(handles_const_event_v<q, e_not_handled>);
}

TEST_CASE("handles_event") {
    static_assert(not handles_event_v<p, e_rvalue>);
    static_assert(handles_event_v<p, e_const_lvalue>);
    static_assert(handles_event_v<p, e_both>);
    static_assert(handles_event_v<p, e_by_value>);
    static_assert(handles_event_v<p, e_forwarding_ref>);
    static_assert(not handles_event_v<p, e_not_handled>);

    static_assert(handles_event_v<q, e_forwarding_ref>);
    // Cannot "see" static_assert failure (no ODR-use)
    static_assert(handles_event_v<q, e_not_handled>);
}

TEST_CASE("handles_events") {
    static_assert(
        handles_events_v<p, type_list<e_const_lvalue, e_both, e_by_value>>);
    static_assert(
        not handles_events_v<p, type_list<e_rvalue, e_both, e_by_value>>);
    static_assert(not handles_events_v<
                  p, type_list<e_const_lvalue, e_both, e_not_handled>>);
}

} // namespace tcspc
