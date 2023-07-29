/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/picoquant_t3.hpp"

#include <type_traits>

namespace tcspc {

static_assert(std::is_trivial_v<pqt3_picoharp_event>);
static_assert(std::is_trivial_v<pqt3_hydraharpv1_event>);
static_assert(std::is_trivial_v<pqt3_hydraharpv2_event>);

static_assert(sizeof(pqt3_picoharp_event) == 4);
static_assert(sizeof(pqt3_hydraharpv1_event) == 4);
static_assert(sizeof(pqt3_hydraharpv2_event) == 4);

} // namespace tcspc
