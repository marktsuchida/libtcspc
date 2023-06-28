/*
 * This file is part of libtcspc
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/broadcast.hpp"

#include "libtcspc/discard.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/test_utils.hpp"

using namespace tcspc;

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

static_assert(handles_event_set_v<internal::broadcast<>, event_set<>>);

static_assert(handles_event_set_v<
              internal::broadcast<discard_all<event_set<>>>, event_set<>>);

static_assert(handles_event_set_v<
              internal::broadcast<discard_all<event_set<e0>>>, event_set<e0>>);

static_assert(
    handles_event_set_v<internal::broadcast<discard_all<event_set<e0>>,
                                            discard_all<event_set<e0>>>,
                        event_set<e0>>);

static_assert(
    handles_event_set_v<internal::broadcast<discard_all<event_set<e0>>,
                                            discard_all<event_set<e0, e1>>>,
                        event_set<e0>>);
