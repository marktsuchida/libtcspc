/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/DynamicPolymorphism.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(
    handles_event_set_v<polymorphic_processor<event_set<>>, event_set<>>);
static_assert(!handles_event_set_v<polymorphic_processor<event_set<>>,
                                   event_set<Event<0>>>);
static_assert(handles_event_set_v<polymorphic_processor<event_set<Event<0>>>,
                                  event_set<Event<0>>>);
static_assert(handles_event_set_v<polymorphic_processor<Events01>, Events01>);

// handles_event_set_v works even if the functions are virtual.
static_assert(
    handles_event_set_v<virtual_processor<event_set<>>, event_set<>>);
static_assert(
    !handles_event_set_v<virtual_processor<event_set<>>, event_set<Event<0>>>);
static_assert(handles_event_set_v<virtual_processor<event_set<Event<0>>>,
                                  event_set<Event<0>>>);
static_assert(handles_event_set_v<virtual_processor<Events01>, Events01>);

static_assert(handles_event_set_v<
              virtual_wrapped_processor<discard_all<event_set<>>, event_set<>>,
              event_set<>>);
static_assert(!handles_event_set_v<
              virtual_wrapped_processor<discard_all<event_set<>>, event_set<>>,
              event_set<Event<0>>>);
static_assert(handles_event_set_v<
              virtual_wrapped_processor<discard_all<event_set<Event<0>>>,
                                        event_set<Event<0>>>,
              event_set<Event<0>>>);
static_assert(
    handles_event_set_v<
        virtual_wrapped_processor<discard_all<Events01>, Events01>, Events01>);
