/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/broadcast.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/test_utils.hpp"

using namespace tcspc;

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

static_assert(handles_event_set_v<internal::broadcast<>, event_set<>>,
              "Broadcast with 0 downstreams should handle empty event set");

// Any event is accepted when there are no downstreams, because it never needs
// to be sent downstream.
static_assert(handles_event_set_v<internal::broadcast<>, event_set<e0>>,
              "Broadcast with 0 downstreams should handle any event");

static_assert(
    handles_event_set_v<internal::broadcast<event_set_sink<event_set<e0>>>,
                        event_set<e0>>,
    "Broadcast should handle events handled by downstream");

static_assert(
    not handles_event_set_v<internal::broadcast<event_set_sink<event_set<e0>>>,
                            event_set<e1>>,
    "Broadcast should not handle events not handled by downstream");

static_assert(
    handles_event_set_v<internal::broadcast<event_set_sink<event_set<e0, e1>>,
                                            event_set_sink<event_set<e0>>>,
                        event_set<e0>>,
    "Broadcast should handle events handled by all downstreams");

static_assert(
    not handles_event_set_v<
        internal::broadcast<event_set_sink<event_set<e0, e1>>,
                            event_set_sink<event_set<e0>>>,
        event_set<e1>>,
    "Broadcast should not handle events not handled by all downstreams");
