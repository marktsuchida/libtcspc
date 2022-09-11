/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/PQT3DeviceEvents.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"

using namespace flimevt;

using discard_tcspc = discard_all<tcspc_events>;

static_assert(
    handles_event_set_v<decode_pq_pico_t3<discard_tcspc>, pq_pico_t3_events>);
static_assert(handles_event_set_v<decode_pq_hydra_v1_t3<discard_tcspc>,
                                  pq_hydra_v1_t3_events>);
static_assert(handles_event_set_v<decode_pq_hydra_v2_t3<discard_tcspc>,
                                  pq_hydra_v2_t3_events>);
