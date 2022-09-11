/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/Broadcast.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(handles_event_set_v<broadcast<>, event_set<>>);

static_assert(
    handles_event_set_v<broadcast<discard_all<event_set<>>>, event_set<>>);

static_assert(
    handles_event_set_v<broadcast<discard_all<event_set<test_event<0>>>>,
                        event_set<test_event<0>>>);

static_assert(
    handles_event_set_v<broadcast<discard_all<event_set<test_event<0>>>,
                                  discard_all<event_set<test_event<0>>>>,
                        event_set<test_event<0>>>);

static_assert(handles_event_set_v<
              broadcast<discard_all<event_set<test_event<0>>>,
                        discard_all<event_set<test_event<0>, test_event<1>>>>,
              event_set<test_event<0>>>);
