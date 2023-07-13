/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/event_set.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

#include <sstream>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

struct my_event1_processor {
    void handle_event([[maybe_unused]] e0 const &event) noexcept {}
};

struct my_end_processor {
    void
    handle_end([[maybe_unused]] std::exception_ptr const &error) noexcept {}
};

} // namespace

static_assert(!contains_event_v<event_set<>, e0>);
static_assert(contains_event_v<event_set<e0>, e0>);
static_assert(!contains_event_v<event_set<e1>, e0>);
static_assert(contains_event_v<event_set<e0, e1>, e0>);
static_assert(contains_event_v<event_set<e0, e1>, e1>);

static_assert(handles_event_v<my_event1_processor, e0>);
static_assert(!handles_event_v<my_event1_processor, e1>);
static_assert(!handles_end_v<my_event1_processor>);
static_assert(!handles_event_set_v<my_event1_processor, event_set<e0>>);

static_assert(!handles_event_v<my_end_processor, e0>);
static_assert(handles_end_v<my_end_processor>);
static_assert(handles_event_set_v<my_end_processor, event_set<>>);

struct my_event1_set_processor : my_event1_processor, my_end_processor {};

static_assert(handles_event_v<my_event1_set_processor, e0>);
static_assert(handles_end_v<my_event1_set_processor>);
static_assert(handles_event_set_v<my_event1_set_processor, event_set<e0>>);
static_assert(!handles_event_set_v<my_event1_set_processor, event_set<e1>>);
static_assert(
    !handles_event_set_v<my_event1_set_processor, event_set<e0, e1>>);

static_assert(std::is_same_v<concat_event_set_t<event_set<e0>, event_set<e1>>,
                             event_set<e0, e1>>);

static_assert(
    std::is_base_of_v<std::variant<e0, e1>, event_variant<event_set<e0, e1>>>);

namespace {

struct my_event {
    friend auto operator<<(std::ostream &stream, my_event const &)
        -> std::ostream & {
        return stream << "expected output";
    }
};

} // namespace

TEST_CASE("Stream insertion of event_variant", "[event_variant]") {
    using ev = event_variant<event_set<my_event>>;
    ev const instance(my_event{});
    std::ostringstream stream;
    stream << instance;
    CHECK(stream.str() == "expected output");
}

TEST_CASE("Equality comparison of event_variant", "[event_variant]") {
    // Demonstrate that event_variant inherits members and operators from
    // std::variant.
    using ev = event_variant<event_set<int, double>>;
    ev const i0(int(42));
    ev const i1(int(42));
    ev const id(double(3.14));
    CHECK(i0 == i1);
    CHECK_FALSE(i0 != i1);
    CHECK_FALSE(i0 == id);
    CHECK(i0 != id);
}

} // namespace tcspc
