/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

// This file may be used in the future to include all the main headers.
// For now, it only serves to document the namespace and Doxygen groups.

/**
 * \mainpage libtcspc Documentation
 *
 * All public symbols are in the namespace \ref tcspc. See the Modules page for
 * a categorized list.
 */

/**
 * \brief libtcspc namespace.
 */
namespace tcspc {

/**
 * \defgroup events Event types
 *
 * \brief Event types.
 */

/**
 * \defgroup processors Processors
 *
 * \brief Event processors.
 */

/**
 * \defgroup timing-generators Timing generators
 *
 * \brief Timing generators for use with \ref generate_timings.
 */

/**
 * \defgroup data-mappers Data mappers
 *
 * \brief Datapoint mappers for use with \ref map_to_datapoints.
 */

/**
 * \defgroup bin-mapper Bin mappers
 *
 * \brief Bin mappers for use with \ref map_to_bins.
 */

// TODO Exceptions, overflow strategies

// TODO We probably need a catch-all group (misc) so that it is easy to look at
// "everything else".

} // namespace tcspc
