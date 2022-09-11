/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/EventSet.hpp"

#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(std::is_same_v<event_variant<event_set<>>, std::variant<>>);
static_assert(
    std::is_same_v<event_variant<event_set<test_event<0>, test_event<1>>>,
                   std::variant<test_event<0>, test_event<1>>>);

static_assert(!contains_event_v<event_set<>, test_event<0>>);
static_assert(contains_event_v<event_set<test_event<0>>, test_event<0>>);
static_assert(!contains_event_v<event_set<test_event<1>>, test_event<0>>);
static_assert(
    contains_event_v<event_set<test_event<0>, test_event<1>>, test_event<0>>);
static_assert(
    contains_event_v<event_set<test_event<0>, test_event<1>>, test_event<1>>);

struct my_event1_processor {
    void handle_event(test_event<0> const &) noexcept {}
};

static_assert(handles_event_v<my_event1_processor, test_event<0>>);
static_assert(!handles_event_v<my_event1_processor, test_event<1>>);
static_assert(!handles_end_v<my_event1_processor>);
static_assert(
    !handles_event_set_v<my_event1_processor, event_set<test_event<0>>>);

struct my_end_processor {
    void handle_end(std::exception_ptr) noexcept {}
};

static_assert(!handles_event_v<my_end_processor, test_event<0>>);
static_assert(handles_end_v<my_end_processor>);
static_assert(handles_event_set_v<my_end_processor, event_set<>>);

struct my_event1_set_processor : my_event1_processor, my_end_processor {};

static_assert(handles_event_v<my_event1_set_processor, test_event<0>>);
static_assert(handles_end_v<my_event1_set_processor>);
static_assert(
    handles_event_set_v<my_event1_set_processor, event_set<test_event<0>>>);
static_assert(
    !handles_event_set_v<my_event1_set_processor, event_set<test_event<1>>>);
static_assert(!handles_event_set_v<my_event1_set_processor,
                                   event_set<test_event<0>, test_event<1>>>);

static_assert(std::is_same_v<concat_event_set_t<event_set<test_event<0>>,
                                                event_set<test_event<1>>>,
                             event_set<test_event<0>, test_event<1>>>);
