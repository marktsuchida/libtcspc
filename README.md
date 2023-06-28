<!--
This file is part of FLIMEvents
Copyright 2019-2022 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# FLIMEvents

**Status: not ready for general use**. An old version of the code is used by
the OpenScan-BHSPC module (code for that version is embedded in that project).

FLIMEvents is a C++ header-only library for handling live photon timestamp
event streams produced by TCSPC hardware. It can decode the raw event records
and produce FLIM histogram or intensity images.

FLIMEvents supports Becker & Hickl event streams (called "FIFO" data by BH). It
can also support PicoQuant event streams (called "TTTR" data by PicoQuant), but
this is untested.

FLIMEvents has no external dependencies other than standard C++.

## How to build

FLIMEvents is a header-only library, so there is nothing to build. Just add the
`include/` directory to your project's include path.

To build unit tests and examples, use Meson.

## Performance

It should be noted that FLIMEvents benefits greatly from compiler optimization.
With Visual C++, an improvement of over 50-fold was observed between Debug
(`/MDd /Od /RTC1`) and Release (`/MD /O2 /Oi /GL`). For live histogramming
during acquisition, an optimized build may be important. (Note that the Meson
build uses different flags by default.)

## Non-goals

FLIMEvents is about photon timestamps and histograms, not file I/O and metadata
handling. Supporting reading/writing of vendor-specific file formats (such as
`.spc`, `.sdt`, `.ptu`, `.phu`) is out of scope. A separate library should
handle that (although the example programs breaks this rule for `.spc`).

## Overview

FLIMEvents takes TCSPC device-generated photon event streams and turns them
into FLIM histogram images. This is done by using a generic event processing
pipeline.

### Namespace

Everything is in the `tcspc` namespace.

### Events

Events represent the data that is passed down the processing pipelines.

Some of these form class hierarchies, but only to model common fields. They do
not have virtual methods, and the inheritance relationship is not significant
in how they are processed.

Event classes are pure data and have public data members.

### Processors

Processors are just classes with the following member functions:

- `void handle_event(E const& event) noexcept` -- a statically polymorphic
  (i.e., overloaded for different event types) function, for each event in the
  event set that is processed by the processor
- `void handle_end(std::exception_ptr error) noexcept` -- `error` is the null
  value (default-constructed `exception_ptr`) when the event stream ends
  successfully

The last argument to the constructor of a processor is usually
`D&& downstream`, a reference to the downstream processor that will handle the
events emitted by this processor. The downstream processor is moved into the
processor.
