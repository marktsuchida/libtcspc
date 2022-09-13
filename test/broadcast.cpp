/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/broadcast.hpp"

#include "flimevt/discard.hpp"
#include "flimevt/event_set.hpp"

#include "test_events.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(handles_event_set_v<internal::broadcast<>, event_set<>>);

static_assert(handles_event_set_v<
              internal::broadcast<discard_all<event_set<>>>, event_set<>>);

static_assert(handles_event_set_v<
              internal::broadcast<discard_all<event_set<test_event<0>>>>,
              event_set<test_event<0>>>);

static_assert(handles_event_set_v<
              internal::broadcast<discard_all<event_set<test_event<0>>>,
                                  discard_all<event_set<test_event<0>>>>,
              event_set<test_event<0>>>);

static_assert(handles_event_set_v<
              internal::broadcast<
                  discard_all<event_set<test_event<0>>>,
                  discard_all<event_set<test_event<0>, test_event<1>>>>,
              event_set<test_event<0>>>);
