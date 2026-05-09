/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <chrono>
#include <thread>

inline void wait_a_little() noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
