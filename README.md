<!--
This file is part of libtcspc
Copyright 2019-2024 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# libtcspc

**Status: C++ library useable but no stable API; Python bindings not ready**.

`libtcspc` is a C++17 header-only library for processing photon timestamp event
streams produced by TCSPC (time-correlated single photon counting) or time
tagging hardware. It is designed for live stream processing of data being
acquired or replayed from disk.

## How to build

`libtcspc` is a header-only library, so there is nothing to build. Just add the
`include/` directory to your project's include path.

To build unit tests and examples, use Meson.

## Requirements

The Python library does not support macOS arm64 (Apple Silicon) due to
limitation in exception handling in current versions of cppyy
([wlav/cppyy#68](https://github.com/wlav/cppyy/issues/68); exceptions thrown in
C++ compiled by cppyy cannot be caught in C++).

## Performance

It should be noted that `libtcspc` benefits greatly from compiler optimization.
With Visual C++, an improvement of over 50-fold was observed between Debug
(`/MDd /Od /RTC1`) and Release (`/MD /O2 /Oi /GL`) in an early test.

## Non-goals

`libtcspc` is about photon timestamp streams, histograms, and other streaming
or in-memory data, not file format and metadata handling. Although
vendor-specific hardware data streams are supported, supporting the reading and
writing of vendor-specific disk file formats (such as `.spc`, `.sdt`, `.ptu`,
`.phu`) is out of scope. A separate library should handle that.
