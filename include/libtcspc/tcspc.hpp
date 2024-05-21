/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

// IWYU pragma: begin_exports
#include "acquire.hpp"
#include "arg_wrappers.hpp"
#include "batch.hpp"
#include "batch_unbatch_from_bytes.hpp"
#include "bh_spc.hpp"
#include "binning.hpp"
#include "bucket.hpp"
#include "buffer.hpp"
#include "check.hpp"
#include "common.hpp"
#include "context.hpp"
#include "core.hpp"
#include "count.hpp"
#include "data_types.hpp"
#include "delay.hpp"
#include "dither.hpp"
#include "errors.hpp"
#include "fit_sequence.hpp"
#include "gate.hpp"
#include "generate.hpp"
#include "histogram.hpp"
#include "histogram_events.hpp"
#include "histogram_policy.hpp"
#include "histogram_scans.hpp"
#include "int_arith.hpp"
#include "int_types.hpp"
#include "introspect.hpp"
#include "match.hpp"
#include "merge.hpp"
#include "move_only_any.hpp"
#include "multiplex.hpp"
#include "npint.hpp"
#include "npint_ops.hpp"
#include "pair.hpp"
#include "picoquant_t2.hpp"
#include "picoquant_t3.hpp"
#include "prepend_append.hpp"
#include "processor_traits.hpp"
#include "read_binary_stream.hpp"
#include "read_integers.hpp"
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
 * - See [Topics](topics.html) for a full table of contents.
 * - See namespace \ref tcspc for an almost-flat list of the public symbols.
 *
 * libtcspc requires C++17 or later.
 *
 * You should include `libtcspc/tcspc.hpp`, which includes all the other
 * headers. (Including individual headers is not recommended because their
 * contents may be moved around in future versions.)
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
 * Events are pieces of data passed from processor to processor.
 *
 * Events in libtcspc are typically plain C++ structs with public data members
 * of simple types. Processors that operate on different event types do so by
 * static polymorphism, so event types do not need to derive from any class.
 *
 * Event types are be movable, copyable, swappable, and default-constructible.
 * This allows buffering when necessary and also simplifies unit testing of
 * processors).
 *
 * Events provided by libtcspc also support equality comparison (`operator==`
 * and `operator!=`) and stream insertion (`operator<<`). This is mostly to
 * facilitate unit testing of processors.
 *
 * Event types and templates are named with the suffix `_event`.
 *
 * Events that carry large amounts of data (such as histograms and arrays
 * thereof) do so using the `tcspc::bucket` type in order to allow zero-copy
 * transfer to various destinations. Bare `tcspc::bucket` instances are also
 * used as events when an array of elementary events (or raw bytes) is produced
 * in a single batch (such as when reading from a file or explicitly collecting
 * for the purpose of buffering).
 *
 * @{
 */

/**
 * \defgroup events-core Core event types
 *
 * \brief General-purpose events.
 */

/**
 * \defgroup events-device Device event types
 *
 * \brief Vendor-specific raw binary records generated by hardware.
 *
 * These events are directly `memcpy()`-able from the raw data stream. They
 * have a public data member, `std::array<std::byte, N> bytes`, where \e N
 * depends on the event type. Accessor functions are provided to interpret the
 * bit/byte fields.
 *
 * @{
 */

/**
 * \defgroup events-bh Becker & Hickl device event types
 *
 * \brief Device event types for BH SPC modules.
 */

/**
 * \defgroup events-pq PicoQuant device event types
 *
 * \brief Device event types for PicoQuant T2 and T3 formats.
 */

/**
 * \defgroup events-swabian Swabian device event types
 *
 * \brief Device event type for Swabian Time Tagger.
 */

/** @} <!-- group events-device --> */

/**
 * \defgroup events-tcspc Time tag and TCSPC event types
 *
 * \brief Logical events translated from raw device events.
 *
 * Most of these events have an `abstime` field containing the absolute time
 * (a.k.a. macrotime) of the event recorded by hardware. Usually the abstime is
 * a monotonically increasing (non-decreasing) timestamp (but it is a good idea
 * to verify this).
 *
 * The abstime is an integer type whose values are in device units. Converting
 * abstime to physical units would cause loss of information (and, in some
 * cases, histogram artifacts); therefore libtcspc preserves exact discretized
 * values for data. Conversion to physical units should be done when displaying
 * final results to the user and is outside the scope of libtcspc.
 *
 * Many of these events have a `channel` field containing the hardware channel
 * on which the event was detected (a signed or unsigned integer whose range
 * depends on the device). Note that some devices have more than one "channel
 * space": for example, detection channels and marker channels may use an
 * overlapping range of numbers despite being distinct.
 *
 * @{
 */

/**
 * \defgroup events-tcspc-lost Lost data event types
 *
 * \brief Events conveying information about lost detection counts.
 */

/** @} <!-- group events-tcspc --> */

/**
 * \defgroup events-timing-modeling Timing modeling event types
 *
 * \brief Events conveying real-valued models of timings and event sequences.
 */

/**
 * \defgroup events-binning Binning event types
 *
 * \brief Events for binning data for histogramming.
 */

/**
 * \defgroup events-histogram Histogram event types
 *
 * \brief Events carrying histograms and arrays of histograms.
 */

/**
 * \defgroup events-testing Testing event types
 *
 * \brief Events used for unit testing of generic processors.
 */

/** @} <!-- group events --> */

/**
 * \defgroup processors Processors
 *
 * \brief Event processors.
 *
 * Processors in libtcspc are usually classes defined in an internal namesapce.
 * They are exposed in the API through factory functions named after a verb
 * describing what the processor does and return the processor by value. A few
 * special processor factory functions (e.g., tcspc::merge()) return multiple
 * processors that can be assigned via structured binding. The factory
 * function, by convention, takes the downstream processor as the last
 * parameter and takes ownership of it (or copies it, if lvalue).
 *
 * All processors are movable but not necessarily copyable.
 *
 * All processors (except for sinks) have a downstream processor. This
 * downstream processor is moved into the next-upstream processor, so that an
 * assembled processing graph is a single object (often with a very long type
 * name). A few special processors (e.g., tcspc::broadcast(), tcspc::route())
 * have multiple downstream processors. Also, some special processors (e.g.,
 * tcspc::merge(), tcspc::type_erased_processor) do not contain their
 * downstream as a subobject but they always own the downstream processor(s)
 * even if by reference.
 *
 * Thus a graph of processors can be built, but this must be done from
 * downstream to upstream.
 *
 * Once built, the processing graph operates in push mode: events are passed
 * from upstream processors to downstream processors by function calls. Each
 * processor is basically a state machine that changes state based on events
 * received, and in some cases emits events to the downstream processor(s). The
 * set of event types accepted by a given processor is determine by C++ type
 * rules based on the processor's `handle()` member function overload set. The
 * end of the stream of events is signaled down the chain of processors via the
 * `flush()` member function. Processing may also terminate due to an error
 * (see below).
 *
 * Processor factory functions never call `handle()` or `flush()` on
 * downstream processors. After construction, processors must always be
 * prepared to receive any of its accepted events while processing continues
 * (but they may signal an error if the sequence of events is incorrect).
 * Behavior is undefined if `handle()` or `flush()` is called on a processor
 * that has been flushed or has stopped with an error.
 *
 * Unless specified otherwise, processors operate on a single thread.
 *
 * Processors implement the following member functions (none of which should be
 * `noexcept`):
 * - `void handle(E const &event)`, possibly with multiple overloads and/or
 *   with `E` as a template parameter. These functions handle individual events
 *   by updating the processor's internal state and emitting events downstream
 *   by calling the downstream processor's `handle()` function.
 * - `void handle(E &&event)`. This is optional and not necessary when `event`
 *   is never forwarded downstream or is known to be of trivial type.
 * - `void flush()`, which conveys the end of stream. The processor emits any
 *   remaining events (due, for example, to buffered state), and flushes its
 *   downstream.
 * - Introspection functions (see the return types for details):
 *   - `[[nodiscard]] auto introspect_node() const -> tcspc::processor_info`.
 *   - `[[nodiscard]] auto introspect_graph() const -> tcspc::processor_graph`.
 *
 * **End of processing and error handling**
 *
 * When the input data has reached its end, the `flush()` function is used to
 * propagate this information down the chain of processors, giving them a
 * chance to propagate any remaining events originating from the events already
 * received.
 *
 * A processor's `handle()` and `flush()` functions may throw an exception
 * under two circumstances:
 *
 * - The processor reached a normal end of processing, for example because it
 *   detected the end of the part of the input that is of interest. In this
 *   case, the processor first calls `flush()` on its downstream(s). Then (if
 *   the call to downstream `flush()` did not throw) it throws
 *   tcspc::end_of_processing (derived from `std::exception`).
 * - The processor encountered an error. In this case it throws an appropriate
 *   exception (always derived from `std::exception`) _without_ flushing the
 *   downstream.
 *
 * **Warnings**
 *
 * For recoverable errors, some processors emit tcspc::warning_event rather
 * than throwing an exception. The tcspc::stop() and tcspc::stop_with_error()
 * processors can be used to end processing on a warning event.
 *
 * **Context, trackers, and accessors**
 *
 * See tcspc::context.
 *
 * **Guidelines for writing processors**
 *
 * In addition to following what is specified above:
 * - Processor constructors (or factory functions) should check arguments and
 *   throw `std::logic_error` (or one of its derived exceptions) if incorrect.
 *   This is for playing nicely with the Python bindings; do not use
 *   `assert()`.
 * - The downstream processor should usually be the last non-static data member
 *   of the processor class, so that the overall data layout mirrors the order
 *   of processing. Cold data (such as data that is only accessed when
 *   finished processing) should be placed after the downstream member.
 * - Ordinary data-processing processors should follow the Rule of Zero. Avoid
 *   const or reference data members.
 * - When passing an lvalue (local variable or data member) event downstream,
 *   and the event value needs to be reused afterwards, the event should be
 *   passed using `std::as_const()`. Conversely, if the local event will not be
 *   reused, it may be passed using `std::move()`.
 * - The `handle()` member function is often overloaded for multiple event
 *   types, some of which may be template parameters, possibly with SFINAE
 *   (`std::enable_if_t`). Choose carefully between `std::enable_if_t` and
 *   `static_assert` when specifying requirements on the handled event types,
 *   because they have different implications. Requirements specified by
 *   `std::enable_if_t` are detected by `tcspc::handles_event` and will prevent
 *   the overload from competing with other overloads when not satisfied.
 *   Requirements specified by `static_assert`, if not satisfied, will cause a
 *   compile error _after_ the overload has been selected. There are use cases
 *   for both.
 * - When implementing `handle()` for both const lvalue and rvalue references,
 *   make sure that a generic forwarding reference handler does not shadow a
 *   specific const lvalue handler.
 *
 * @{
 */

/**
 * \defgroup processors-core Core processors
 *
 * \brief Basic and generic processors.
 *
 * @{
 */

/**
 * \defgroup processors-filtering Filtering processors
 *
 * \brief Processors for filtering events.
 */

/**
 * \defgroup processors-batching Batching and unbatching processors
 *
 * \brief Processors that aggregate events into batches or extract individual
 * events from batches.
 */

/**
 * \defgroup processors-mux Multiplexing and demultiplexing processors
 *
 * \brief Processors that combine events of different types into a variant type
 * and back.
 */

/**
 * \defgroup processors-stopping Stopping processors
 *
 * \brief Processors that stop processing when a given event is received.
 */

/** @} <!-- group processors-core --> */

/**
 * \defgroup processors-buffering Buffering processors
 *
 * \brief Processors for buffering data.
 */

/**
 * \defgroup processors-branching Branching processors
 *
 * \brief Processors for splitting the processing graph.
 */

/**
 * \defgroup processors-merging Merging processors
 *
 * \brief Processors for joining branches in the processing graph.
 */

/**
 * \defgroup processors-io Input and output processors
 *
 * \brief Processors for reading and writing data from/to file-like streams.
 *
 * @{
 */

/**
 * \defgroup processors-binary Binary stream processors
 *
 * \brief Processors for converting between events and binary data streams.
 */

/** @} <!-- group processors-io --> */

/**
 * \defgroup processors-acquisition Acquisition processors
 *
 * \brief Processors for acquiring data from hardware devices.
 */

/**
 * \defgroup processors-decoding Decoding processors
 *
 * \brief Processors for decoding device events.
 *
 * @{
 */

/**
 * \defgroup processors-bh Becker & Hickl decoding processors
 *
 * \brief Processors for decoding Becker & Hickl SPC device events.
 */

/**
 * \defgroup processors-pq PicoQuant decoding processors
 *
 * \brief Processors for decoding PicoQuant T2 and T3 device events.
 */

/**
 * \defgroup processors-swabian Swabian decoding processors
 *
 * \brief Processors for decoding Swabian Time Tagger device events.
 */

/** @} <!-- group processors-decoding --> */

/**
 * \defgroup processors-timeline Timeline processors
 *
 * \brief Processors for managing and manipulating the absolute timeline.
 */

/**
 * \defgroup processors-timing Timing signal processors
 *
 * \brief Processors for transforming timing signal events.
 *
 * @{
 */

/**
 * \defgroup processors-timing-modeling Timing signal modeling processors
 *
 * \brief Processors for fitting and extrapolating timing models.
 */

/** @} <!-- group processors-timing --> */

/**
 * \defgroup processors-time-corr Time correlation processors
 *
 * \brief Processors for time correlation.
 *
 * @{
 */

/**
 * \defgroup processors-pairing Pairing processors
 *
 * \brief Processors for finding pairs of detection events.
 */

/** @} <!-- group processors-time-corr --> */

/**
 * \defgroup processors-histogramming Histogramming processors
 *
 * \brief Processors for histograming.
 *
 * @{
 */

/**
 * \defgroup processors-binning Binning processors
 *
 * \brief Processors for binning data for histogramming.
 */

/** @} <!-- group processors-histogramming --> */

/**
 * \defgroup processors-validation Validation processors
 *
 * \brief Processors for data validation.
 */

/**
 * \defgroup processors-stats Statistics processors
 *
 * \brief Processors for collecting statistics.
 */

/**
 * \defgroup processors-testing Testing processors
 *
 * \brief Processors for unit testing of processors.
 */

/** @} <!-- group processors --> */

/**
 * \defgroup data-types Data type sets
 *
 * \brief Bundle of numeric types used by events and processors.
 *
 * Many events and processors in libtcspc deal with multiple integer types, so
 * specifying them individually would be cumbersome. We therefore usually
 * specify them as a single unit called the _data type set_ (usually template
 * parameter `DataTypes`), which is a type containing several type aliases to
 * be used across a processing graph (or part of a processing graph).
 */

/**
 * \defgroup auxiliary Auxiliary types and objects
 *
 * \brief Auxiliary types and objects that define processor behavior.
 *
 * @{
 */

/**
 * \defgroup bucket-sources Bucket sources
 *
 * \brief Objects producing a series of `tcspc::bucket` instances to carry
 * data.
 */

/**
 * \defgroup routers Routers
 *
 * \brief Routers for use with `tcspc::route()` and
 * `tcspc::route_homogeneous()`.
 *
 * Routers implement the function call operator `auto operator()(Event const &)
 * const -> std::size_t` where the `Event` must be overloaded (or templated)
 * for every event handled by the routing processor.
 *
 * The return value is the index of the downstream processor to which the event
 * should be routed. If the index is out of range, the event is discarded.
 * Routers can return `std::numeric_limits<std::size_t>::max()` when the event
 * should be discarded.
 */

/**
 * \defgroup streams-input Input streams
 *
 * \brief Streams for use with `tcspc::read_binary_stream()`
 *
 * An input stream is a movable (usually noncopyable) object with the following
 * member functions:
 * - `auto is_error() noexcept -> bool`:
 *   Return true if the stream is not available or the previous read operation
 *   resulted in an error (\e not including reaching EOF). Not influenced by
 *   failure of `tell()` or `skip()`.
 * - `auto is_eof() noexcept -> bool`:
 *   Return true if the previous read operation tried to read beyond the end of
 *   the stream (or if the stream is not available). Not influenced by failure
 *   of `tell()` or `skip()`.
 * - `auto is_good() noexcept -> bool`:
 *   Return true if neiter `is_error()` nor `is_eof()` is true.
 * - `auto tell() noexcept -> std::optional<std::uint64_t>`:
 *   Return the current stream position if supported by the stream, or
 *   `std::nullopt`.
 * - `auto skip(std::uint64_t bytes) noexcept -> bool`:
 *   Seek, relative to the current offset, forward by \p bytes. Return true if
 *   successful.
 * - `auto read(tcspc::span<std::byte> buffer) noexcept ->
 *   std::uint64_t`:
 *   Read into the given buffer, up to the buffer size. Return the number of
 *   bytes read.
 */

/**
 * \defgroup streams-output Output streams
 *
 * \brief Streams for use with `tcspc::write_binary_stream()`.
 *
 * An output stream is a movable (usually noncopyable) object with the
 * following member functions:
 * - `auto is_error() noexcept -> bool`:
 *   Return true if the stream is not available or the previous write operation
 *   resulted in an error. Not influenced by failure of `tell()`.
 * - `auto tell() noexcept -> std::optional<std::uint64_t>`:
 *   Return the current stream position if supported by the stream, or
 *   `std::nullopt`.
 * - `void write(tcspc::span<std::byte const> buffer) noexcept`:
 *   Write the given bytes to the stream.
 */

/**
 * \defgroup acquisition-readers Readers for acquisition
 *
 * \brief Readers that wrap pull-style device acquisition APIs.
 *
 * A reader is a movable object that defines the function call operator:
 * `auto operator()(span<T> buffer) -> std::optional<std::size_t>`, in which
 * `buffer` is the span into which data should be placed. The return value is
 * `std::nullopt` if the end of the acquired data has been reached; otherwise
 * it is the number of `T` elements actually read; this may be zero. If there
 * was an error, an exception should be thrown.
 */

/**
 * \defgroup timing-generators Timing generators
 *
 * \brief Timing generators for use with `tcspc::generate()`.
 *
 * Timing generators must define the following member functions:
 *
 * - `void trigger(TriggerEvent const &event)`, which starts a new
 *   iteration of timing generation, based on the `abstime` and (optionally)
 *   other fields of the trigger event,
 *
 * - `auto peek() const -> std::optional<abstime_type>`, which returns
 *   the abstime of the next event to be generated, if any, and
 *
 * - `void pop()`, which removes the next abstime,
 *
 * where `abstime_type` is the type of the `abstime` fields of the
 * `tcspc::generate()` processor's `TriggerEvent` and `OutputEvent` type
 * parameters (which must match).
 *
 * `trigger()` may be a template or overload set; it must accept the
 * `tcspc::generate()`'s `TriggerEvent` type.
 *
 * `peek()` must continue to return the same value if called multiple times
 * with no call to `trigger()` or `pop()` in between.
 *
 * The end of a generated sequence of timings from a given trigger is indicated
 * by `peek()` returning `std::nullopt`.
 *
 * It is guaranteed that `pop()` is only called when `peek()` returns an
 * abstime. The timing generator must not produce any timings before the first
 * time it is triggered: `peek()` must return `std::nullopt` when `trigger()`
 * has not yet been called.
 *
 * @{
 */

/**
 * \defgroup timing-generators-dither Dithered timing generators
 *
 * \brief Timing generators using a floating-point model with dithering.
 *
 * Dithering is one way to reduce statistical bias when rounding real (floating
 * point) numbers to integers. It applies noise (the 'dither') to the number
 * before quantization (of which rounding is a special case) so that the
 * quantization error is randomized. This prevents periodic patterns in the
 * error.
 *
 * In libtcspc dither can be applied when generating timing events based on a
 * timing model defined by floating point factors. This can prevent patterned
 * noise in the result, for example, when the timings denote time bins (such as
 * pixels of a raster-scanned image) and the length of the time bins is not
 * very large compared to the abstime unit.
 *
 * When the abstime has much higher resolution than the timing signals,
 * dithering is usually not necessary. Also, it is of course best if the
 * processing can be constrained so that time bins are an exact multiple of the
 * abstime unit.
 */

/** @} <!-- group timing-generators --> */

/**
 * \defgroup matchers Matchers
 *
 * \brief Matchers for use with `tcspc::match` and `tcspc::match_replace`.
 *
 * These objects are essentially predicates: they define the member `auto
 * operator()(Event const &event) const -> bool`, where `Event const &` must be
 * implicitly convertible from the event type being matched by the processor.
 * In general-purpose matchers, `Event` is often a template parameter of the
 * `operator()()`. The function call operator returns true if the `event` was
 * matched; otherwise false.
 *
 * (Note that lambdas can also be used as matchers: `tcspc::always_matcher()`
 * is equivalent to `[](auto const &) { return true; }`.)
 */

/**
 * \defgroup data-mappers Data mappers
 *
 * \brief Datapoint mappers for use with `tcspc::map_to_datapoints`.
 *
 * Data mappers define the member `auto operator()(Event const &) const ->
 * datapoint_type`, where `Event const &` must be implicitly convertible from
 * the event type being mapped by the processor. `Event` can be a template
 * parameter of the `operator()()`.
 *
 * (Note that lambdas can also be used as data mappers.)
 */

/**
 * \defgroup bin-mappers Bin mappers
 *
 * \brief Bin mappers for use with `tcspc::map_to_bins`.
 *
 * Bin mappers define the following members:
 * - `[[nodiscard]] auto n_bins() const -> std::size_t`, which returns the
 *   number of bins to which datapoints are mapped, and
 * - `auto operator()(datapoint_type d) const ->
 *   std::optional<bin_index_type>`, where `datapoint_type` and
 *   `bin_index_type` must match the `tcspc::map_to_bins()` processor's
 *   `DataTypes`. This function call operator mapps the datapoint `d` to a bin
 *   index.
 *
 * (`n_bins()` is provided for convenience and is not used by
 * `tcspc::map_to_bins`. Thus, any invokable, such as a lambda, can be used as
 * a bin mapper.)
 */

/**
 * \defgroup histogram-policy Histogram policy
 *
 * \brief Compile-time flags for specifying how to handle histogram bin
 * overflow and other behavior.
 *
 * Used by `tcspc::histogram()` and `tcspc::histogram_scans()` as non-type
 * template parameter.
 */

/** @} <!-- group auxiliary --> */

/**
 * \defgroup context Context
 *
 * \brief Mechanism providing access to objects (especially processors) after
 * they have been incorporated into a processing graph.
 *
 * From the viewpoint of user code, this works as follows:
 *
 * -# Create a `tcspc::context`.
 * -# Build the processing graph. Some processors (and auxiliary objects)
 *    require a `tcspc::access_tracker`; obtain trackers from the context
 *    (specifying a uniquely identifying name).
 * -# The processors and other objects, having been moved (or copied) into the
 *    graph, are not directly accessible. However, corresponding _access_
 *    objects can be obtained from the context by name.
 *
 * From the viewpoint of the object that provides access through the context,
 * this works as follows:
 *
 * -# On construction, the object receives a `tcspc::access_tracker` (a
 *    movable but noncopyable object) and stores it in a data member.
 * -# Also during construction, the object calls the tracker's
 *    `tcspc::access_tracker::register_access_factory()` member function,
 *    passing the _access factory_, which is a function (usually a lambda)
 *    taking a reference to the tracker and returning an _accessor_.
 * -# The tracker stored in the member variable tracks the object as it is
 *    moved or destroyed, updating the associated context's mapping from name
 *    to tracker address.
 * -# When user code retrieves an accessor from the context, the access factory
 *    is called with a reference to the (potentially moved) tracker. The access
 *    factory usually converts this to a reference (address) to the object, and
 *    returns an accessor object holding a reference to the object.
 *
 * The macro #LIBTCSPC_OBJECT_FROM_TRACKER() can be used by access factories
 * to obtain the address of an object holding a tracker in a data member.
 *
 * @{
 */

/**
 * \defgroup context-access Accessor types
 *
 * \brief Objects providing access via context.
 */

/** @} <!-- group context --> */

/**
 * \defgroup introspect Introspection
 *
 * \brief Processor introspection and Graphviz graph generation.
 *
 * \note Proceessor info and graph expose implementation details that may not
 * be stable. It is intended primarily for visualization, debugging, and
 * testing; not as a basis for automation.
 */

/**
 * \defgroup exceptions Exceptions
 *
 * \brief Exception types.
 *
 * In libtcspc, exceptions are used to signal the end of processing when a
 * processor either detects an error (see \ref errors) or decides that the end
 * of (the interesting part of) the input has been reached (see
 * `tcspc::end_of_processing`).
 *
 * Usually, exceptions that indicate errors during processing are derived from
 * `std::runtime_error`.  However, other exceptions may also be thrown, such as
 * `std::bad_alloc`.
 *
 * Outside of processing, programming errors such as passing an invalid
 * argument are reported by throwing an exception derived from
 * `std::logic_error`. This choice was made (as opposed to, say, using
 * `assert()`) to facilitate interoperation with environments like Python.
 *
 * \see `tcspc::warning_event`, `tcspc::stop_with_error`
 *
 * @{
 */

/**
 * \defgroup errors Errors
 *
 * \brief Exceptions representing errors generated by processors.
 */

/** @} <!-- group exceptions --> */

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
 * \defgroup integers Integers
 *
 * \brief Facilities for reading integers from bit-packed records.
 */

/**
 * \defgroup arg-wrappers Function argument wrappers
 *
 * \brief Trivial types for strong typing of function arguments.
 */

/**
 * \defgroup misc Miscellaneous
 *
 * \brief Other utilities.
 */

} // namespace tcspc
