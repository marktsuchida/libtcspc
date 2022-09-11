/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/dynamic_polymorphism.hpp"

#include "flimevt/discard.hpp"
#include "flimevt/event_set.hpp"

#include "test_events.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(
    handles_event_set_v<polymorphic_processor<event_set<>>, event_set<>>);
static_assert(!handles_event_set_v<polymorphic_processor<event_set<>>,
                                   event_set<test_event<0>>>);
static_assert(
    handles_event_set_v<polymorphic_processor<event_set<test_event<0>>>,
                        event_set<test_event<0>>>);
static_assert(handles_event_set_v<polymorphic_processor<test_events_01>,
                                  test_events_01>);

// handles_event_set_v works even if the functions are virtual.
static_assert(
    handles_event_set_v<virtual_processor<event_set<>>, event_set<>>);
static_assert(!handles_event_set_v<virtual_processor<event_set<>>,
                                   event_set<test_event<0>>>);
static_assert(handles_event_set_v<virtual_processor<event_set<test_event<0>>>,
                                  event_set<test_event<0>>>);
static_assert(
    handles_event_set_v<virtual_processor<test_events_01>, test_events_01>);

static_assert(handles_event_set_v<
              virtual_wrapped_processor<discard_all<event_set<>>, event_set<>>,
              event_set<>>);
static_assert(!handles_event_set_v<
              virtual_wrapped_processor<discard_all<event_set<>>, event_set<>>,
              event_set<test_event<0>>>);
static_assert(handles_event_set_v<
              virtual_wrapped_processor<discard_all<event_set<test_event<0>>>,
                                        event_set<test_event<0>>>,
              event_set<test_event<0>>>);
static_assert(
    handles_event_set_v<
        virtual_wrapped_processor<discard_all<test_events_01>, test_events_01>,
        test_events_01>);
