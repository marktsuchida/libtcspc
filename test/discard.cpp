/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/discard.hpp"

#include "flimevt/event_set.hpp"

#include "test_events.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(handles_event_set_v<discard_all<event_set<>>, event_set<>>);
static_assert(
    handles_event_set_v<discard_all<test_events_01>, test_events_01>);
static_assert(
    !handles_event_set_v<discard_all<test_events_01>, test_events_23>);
