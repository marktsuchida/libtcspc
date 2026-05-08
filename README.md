<!--
This file is part of libtcspc
Copyright 2019-2024 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# libtcspc

**Status: C++ library usable but no stable API; Python bindings in progress**.

`libtcspc` is a C++20 header-only library for processing photon timestamp event
streams produced by TCSPC (time-correlated single photon counting) or time
tagging hardware. It is designed for live stream processing of data being
acquired or replayed from disk.

## Requirements

The **C++** library requires C++20. Recent versions of GCC, Clang, or MSVC
should work.

The **Python** library (under development) requires Python 3.10 or later and a
C++ build environment (at run time, since C++ code is dynamically generated and
compiled).

- On Windows, Visual Studio (2022+) with "C++ Desktop Development" is required.
- On macOS, Apple's latest Command Line Tools should be sufficient.

## How to build

`libtcspc` for C++ is a header-only library, so there is nothing to build. Just
add the `include/` directory to your project's include path.

To build the unit tests and example programs, use Meson:

```sh
pip install meson ninja
meson setup builddir
meson test -C builddir
```

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
