<!--
This file is part of libtcspc
Copyright 2019-2024 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# libtcspc

**Status: C++ library usable but no stable API; Python bindings in progress**.

`libtcspc` is a C++ 17 header-only library for processing photon timestamp
event streams produced by TCSPC (time-correlated single photon counting) or
time tagging hardware. It is designed for live stream processing of data being
acquired or replayed from disk.

## Requirements

The **C++** library requires C++ 17. The following compiler versions are
supported (that is, it is considered a bug if they don't work). Earlier
versions may work but are not tested.

- Linux: GCC 11 or later; Clang (with libstdc++) 14 or later.
- Windows: MSVC 19.40 (Visual Studio 2022 17.10) or later; clang-cl 18 or
  later. (Visual Studio 2019 does not work.)
- macOS: Apple Clang 15 or later on macOS 13 or later (x86-64 or arm64).

The **Python** library (under development) is based on cppyy, which requires a
C++ build environment. However, the C++ library compiler requirements (above)
do not directly apply because only cppyy is used to compile (at run time)
libtcspc code. See
[cppyy installation instructions](https://cppyy.readthedocs.io/en/latest/installation.html).
Python 3.10 or later is required.

- On Windows, Visual Studio with "C++ Desktop Development" is required. Make
  sure to work inside the "Developer PowerShell (or Command Prompt) for Visual
  Studio".
- On Windows, the environment variable `STDCXX` must be set to `17` when
  running with Visual Studio 2022
  ([wlav/cppyy#208](https://github.com/wlav/cppyy/issues/208#issuecomment-1928461467)).
- On macOS (Intel), Apple's latest Command Line Tools should be sufficient. If
  using an ARM machine, install Rosetta 2 and use Conda to install an Intel
  build of Python. macOS 13 or later is required for a new enough C++ standard
  library.

macOS arm64 (Apple Silicon) is not currently supported due to the lack of
correct exception handling
([wlav/cppyy#68](https://github.com/wlav/cppyy/issues/68) due to an upstream
LLVM limitation; exceptions thrown in C++ compiled by cppyy cannot be caught in
C++).

When using the Python library:

- The system compiler (GCC, MSVC, Apple Clang) is used to build parts of cppyy
  during installation.
- Cppyy compiles libtcspc's C++ code.
- The C++ standard library comes from the system.

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
