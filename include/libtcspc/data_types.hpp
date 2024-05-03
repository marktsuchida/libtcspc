/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "int_types.hpp"

namespace tcspc {

/**
 * \brief Default set of integer data types.
 *
 * Many events and processors in libtcspc deal with multiple integer types, so
 * specifying them individually would be cumbersome. We therefore usually
 * specify them as a single unit called the _data type set_ (usually template
 * parameter `DataTypes`), which is a type containing several type aliases to
 * be used across a processing graph (or part of a processing graph). This is
 * the default data type set.
 *
 * \ingroup misc
 */
struct default_data_types {
    /**
     * \brief Absolute time type.
     *
     * The default of `i64` is chosen because 64-bit precision is reasonable
     * (32-bit would overflow; 128-bit would hurt performance and is not
     * required for most applications) and because we want to allow negative
     * time stamps.
     */
    using abstime_type = i64;

    /**
     * \brief Channel number type.
     */
    using channel_type = i32;

    /**
     * \brief Difference time type.
     */
    using difftime_type = i32;

    /**
     * \brief Count type.
     *
     * This is used in events carrying a count of detections.
     */
    using count_type = u32;

    /**
     * \brief Type of datapoint for histogramming.
     */
    using datapoint_type = i32;

    /**
     * \brief Type of histogram bin index.
     */
    using bin_index_type = u16;

    /**
     * \brief Type of histogram bin value (count).
     */
    using bin_type = u16;
};

} // namespace tcspc
