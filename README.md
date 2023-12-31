<!--
This file is part of libtcspc
Copyright 2019-2023 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# libtcspc

**Status: not ready for general use**.

`libtcspc` is a C++17 header-only library for processing photon timestamp event
streams produced by TCSPC (time-correlated single photon counting) or time
tagging hardware. It is designed for live stream processing of data being
acquired or replayed from disk.

## How to build

`libtcspc` is a header-only library, so there is nothing to build. Just add the
`include/` directory to your project's include path.

To build unit tests and examples, use Meson.

## Performance

It should be noted that `libtcspc` benefits greatly from compiler optimization.
With Visual C++, an improvement of over 50-fold was observed between Debug
(`/MDd /Od /RTC1`) and Release (`/MD /O2 /Oi /GL`) in an early test.

## Non-goals

`libtcspc` is about photon timestamp streams, histograms, and other streaming
or in-memory data, not file I/O and metadata handling. Although vendor-specific
hardware data streams are supported, supporting the reading and writing of
vendor-specific disk file formats (such as `.spc`, `.sdt`, `.ptu`, `.phu`) is
out of scope. A separate library should handle that.

### Namespace

Everything is in the `tcspc` namespace.

### Usage Overview

All data processing is performed in a "push" mode: when a new event record is
received, it is passed down the chain of processors, possibly getting
converted, augmented, or filtered. Thus, the output data can be kept in a
constantly updated state, up to the information from the last photon or other
event.

In practice, processing can be batched for efficiency using buffering between
processors.

### Events

Events represent the data that is passed down the processing pipelines.

Some of these form class hierarchies, but only to model common fields. They do
not have virtual methods, and the inheritance relationship is not significant
in how they are processed.

Event classes are pure data and have public data members.

### Processors

Processors are just classes with the following member functions:

- `void handle(Event const &event)` -- a statically polymorphic (i.e.,
  overloaded for different event types) function, for each event in the event
  set that is processed by the processor
- `void flush()` -- called when the event stream ends successfully, to flush
  out any events buffered in processors

When creating a processor, the downstream processor is passed in as an argument
and moved in. This results in a single object for a chain of processors, tying
frequently accessed state data in a compact layout and enabling heavy compiler
optimizations on the event processing.
