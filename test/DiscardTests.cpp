/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/Discard.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(handles_event_set_v<discard_all<event_set<>>, event_set<>>);
static_assert(handles_event_set_v<discard_all<Events01>, Events01>);
static_assert(!handles_event_set_v<discard_all<Events01>, Events23>);
