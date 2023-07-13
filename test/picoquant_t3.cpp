/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/picoquant_t3.hpp"

#include <type_traits>

namespace tcspc {

static_assert(std::is_trivial_v<pq_pico_t3_event>);
static_assert(std::is_trivial_v<pq_hydra_v1_t3_event>);
static_assert(std::is_trivial_v<pq_hydra_v2_t3_event>);

static_assert(sizeof(pq_pico_t3_event) == 4);
static_assert(sizeof(pq_hydra_v1_t3_event) == 4);
static_assert(sizeof(pq_hydra_v2_t3_event) == 4);

} // namespace tcspc
