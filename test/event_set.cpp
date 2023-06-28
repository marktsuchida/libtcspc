/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/event_set.hpp"

#include "libtcspc/test_utils.hpp"

using namespace flimevt;

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

static_assert(std::is_same_v<event_variant<event_set<>>, std::variant<>>);
static_assert(
    std::is_same_v<event_variant<event_set<e0, e1>>, std::variant<e0, e1>>);

static_assert(!contains_event_v<event_set<>, e0>);
static_assert(contains_event_v<event_set<e0>, e0>);
static_assert(!contains_event_v<event_set<e1>, e0>);
static_assert(contains_event_v<event_set<e0, e1>, e0>);
static_assert(contains_event_v<event_set<e0, e1>, e1>);

struct my_event1_processor {
    void handle_event([[maybe_unused]] e0 const &event) noexcept {}
};

static_assert(handles_event_v<my_event1_processor, e0>);
static_assert(!handles_event_v<my_event1_processor, e1>);
static_assert(!handles_end_v<my_event1_processor>);
static_assert(!handles_event_set_v<my_event1_processor, event_set<e0>>);

struct my_end_processor {
    void
    handle_end([[maybe_unused]] std::exception_ptr const &error) noexcept {}
};

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
