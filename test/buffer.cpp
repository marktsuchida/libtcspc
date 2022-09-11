/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/buffer.hpp"

#include "flimevt/discard.hpp"
#include "flimevt/event_set.hpp"

#include "test_events.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(
    handles_event_set_v<
        buffer_event<test_event<0>, discard_all<event_set<test_event<0>>>>,
        event_set<test_event<0>>>);
