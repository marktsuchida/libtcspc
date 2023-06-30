/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/swabian_tag.hpp"

#include <type_traits>

using namespace tcspc;

static_assert(std::is_trivial_v<swabian_tag_event>);

static_assert(sizeof(swabian_tag_event) == 16);
