/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

// IWYU pragma: begin_exports
#include "arg_wrappers.hpp"
#include "batch.hpp"
#include "bh_spc.hpp"
#include "binning.hpp"
#include "buffer.hpp"
#include "check.hpp"
#include "common.hpp"
#include "count.hpp"
#include "delay.hpp"
#include "dither.hpp"
#include "errors.hpp"
#include "fit_sequence.hpp"
#include "gate.hpp"
#include "generate.hpp"
#include "histogram.hpp"
#include "histogram_elementwise.hpp"
#include "histogram_events.hpp"
#include "int_types.hpp"
#include "introspect.hpp"
#include "match.hpp"
#include "merge.hpp"
#include "multiplex.hpp"
#include "npint.hpp"
#include "object_pool.hpp"
#include "own_on_copy_view.hpp"
#include "pair.hpp"
#include "picoquant_t2.hpp"
#include "picoquant_t3.hpp"
#include "processor_context.hpp"
#include "processor_traits.hpp"
#include "read_binary_stream.hpp"
#include "read_bytes.hpp"
#include "recover_order.hpp"
#include "regulate_time_reached.hpp"
#include "route.hpp"
#include "select.hpp"
#include "span.hpp"
#include "stop.hpp"
#include "swabian_tag.hpp"
#include "test_utils.hpp"
#include "time_correlate.hpp"
#include "time_tagged_events.hpp"
#include "timing_misc.hpp"
#include "type_erased_processor.hpp"
#include "type_list.hpp"
#include "variant_event.hpp"
#include "vector_queue.hpp"
#include "view_as_bytes.hpp"
#include "write_binary_stream.hpp"
// IWYU pragma: end_exports

/**
 * \mainpage libtcspc C++ API
 *
 * All public symbols are in the namespace \ref tcspc. See Topics for a
 * categorized list.
 */

/**
 * \brief libtcspc namespace.
 */
namespace tcspc {

/**
 * \defgroup events Event types
 *
 * \brief Event types.
 *
 * @{
 */

/**
 * \defgroup events-basic Simple events
 *
 * \brief Events with no specific meaning attached.
 *
 * @{
 */

/**
 * \defgroup events-variant Variant event and utilities
 *
 * \brief An event type representing a type-safe union of event types.
 */

/** @} <!-- group events-basic --> */

/**
 * \defgroup events-device Device events
 *
 * \brief Raw event records generated by devices.
 */

/**
 * \defgroup events-timing Time tag and TCSPC events
 *
 * \brief Events representing time tags or TCSPC records.
 */

/**
 * \defgroup events-histogram Histogramming events
 *
 * \brief Events related to histogramming.
 */

/**
 * \defgroup events-testing Testing events
 *
 * \brief Events used for unit testing.
 */

/** @} */

/**
 * \defgroup processors Processors
 *
 * \brief Event processors.
 *
 * @{
 */

/**
 * \defgroup processors-basic Building block processors
 *
 * \brief Processors for basic processing tasks.
 */

/**
 * \defgroup processors-decode Decoding processors
 *
 * \brief Processors for decoding device events.
 */

/**
 * \defgroup processors-timing Basic time-tag processing
 *
 * \brief Processors for basic time-tag processing
 */

/**
 * \defgroup processors-histogram Histogramming processors
 *
 * \brief Processors for histogramming.
 */

/**
 * \defgroup processors-testing Testing processors
 *
 * \brief Processors for use in unit tests.
 */

/** @} */

/**
 * \defgroup streams Input and output streams
 *
 * \brief Stream wrappers for use with \ref read_binary_stream and \ref
 * write_binary_stream.
 */

/**
 * \defgroup timing-generators Timing generators
 *
 * \brief Timing generators for use with \ref generate.
 */

/**
 * \defgroup matchers Matchers
 *
 * \brief Matchers for use with \ref match and \ref match_replace.
 */

/**
 * \defgroup routers Routers
 *
 * \brief Routers for use with \ref route.
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

/**
 * \defgroup overflow-strategies Overflow strategies
 *
 * \brief Tag types for specifying histogram bin overflow handling method.
 */

/**
 * \defgroup integers Integer handling
 *
 * \brief Facilities for reading integers from bit-packed records.
 */

/**
 * \defgroup exceptions Exceptions
 *
 * \brief Exceptions representing errors generated by processors.
 */

/**
 * \defgroup introspect Introspection
 *
 * \brief Facilities for introspecting a processor graph.
 */

/**
 * \defgroup type-list Type lists
 *
 * \brief Types and metafunctions for handling lists (or sets) of types.
 */

/**
 * \defgroup processor-traits Processor traits
 *
 * \brief Trait metafunctions to check processor capabilities.
 */

/**
 * \defgroup arg-wrappers Function parameter wrappers
 *
 * \brief Trivial types for strong typing of function arguments.
 */

/**
 * \defgroup misc Miscellaneous
 *
 * \brief Everything else
 */

} // namespace tcspc
