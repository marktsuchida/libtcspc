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

struct MyEvent1Processor {
    void handle_event(test_event<0> const &) noexcept {}
};

static_assert(handles_event_v<MyEvent1Processor, test_event<0>>);
static_assert(!handles_event_v<MyEvent1Processor, test_event<1>>);
static_assert(!handles_end_v<MyEvent1Processor>);
static_assert(
    !handles_event_set_v<MyEvent1Processor, event_set<test_event<0>>>);

struct MyEndProcessor {
    void handle_end(std::exception_ptr) noexcept {}
};

static_assert(!handles_event_v<MyEndProcessor, test_event<0>>);
static_assert(handles_end_v<MyEndProcessor>);
static_assert(handles_event_set_v<MyEndProcessor, event_set<>>);

struct MyEvent1SetProcessor : MyEvent1Processor, MyEndProcessor {};

static_assert(handles_event_v<MyEvent1SetProcessor, test_event<0>>);
static_assert(handles_end_v<MyEvent1SetProcessor>);
static_assert(
    handles_event_set_v<MyEvent1SetProcessor, event_set<test_event<0>>>);
static_assert(
    !handles_event_set_v<MyEvent1SetProcessor, event_set<test_event<1>>>);
static_assert(!handles_event_set_v<MyEvent1SetProcessor,
                                   event_set<test_event<0>, test_event<1>>>);

static_assert(std::is_same_v<concat_event_set_t<event_set<test_event<0>>,
                                                event_set<test_event<1>>>,
                             event_set<test_event<0>, test_event<1>>>);
