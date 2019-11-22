FLIMEvents
==========

Status: alpha (as an independent library), used by OpenScan-BHSPC only

FLIMEvents is a C++ header-only library for handling live photon timestamp
event streams produced by TCSPC hardware. It can decode the raw event records
and produce FLIM histogram or intensity images. It is currently a subdirectory
of OpenScan-BHSPC, and does not have a stable API. In the future, we might
evolve this into a separate library.

FLIMEvents supports Becker & Hickl event streams (called "FIFO" data by BH). It
can also support PicoQuant event streams (called "TTTR" data by PicoQuant), but
this is untested.

FLIMEvents has no external dependencies other than standard C++.

At the moment, FLIMEvents is developed with Visual C++ 2019 on Windows (though
it should be easy to make the code cross-platform; no platform APIs are used).


How to build
------------

FLIMEvents is a header-only library, so there is nothing to build. Just add the
`include/` directory to your project's include path.

To build unit tests and examples, use Meson.

On Windows, open `x64 Native Tools Command Prompt for VS 2019` (or equivalent
for the desired architecture and VC++ version) from the Start menu. Change to
the FLIMEvents root directory.

To build from the command line, `meson build` will create the directory
`build/` containing Ninja build files. Change into `build/` and type `ninja` to
build; `ninja test` to run tests.

To view and build as a Visual Studio project, use `meson build --backend vs`.
This will generate a Visual Studio solution and project files.

Meson builds do not create files outside of the given build directory (which
can be named differently). To perform Release vs Debug builds, two separate
build directories can be configured. Pass `--buildtype debug`, `--buildtype
debugoptimized`, or `--buildtype release` to `meson`.

For example:
```
meson build/release --buildtype release
meson build/debug --buildtype debug
meson build/debug-vs --backend vs --buildtype debug
```


Design overview
---------------

In order to handle live event streams, FLIMEvents uses a stream buffer
(`EventStream`, together with `EventBuffer` and `EventBufferPool`) to buffer
raw events (in fixed-sized batches).

Events can then be processed on a separate thread without blocking data
acquisition. This is done by a series of "processor" objects, all of which
operate in "push" mode (upstream processors call methods of downstream
processors to propagate data).

Currently, there are processors that cooperate to produce FLIM histograms.

The abstract processor classes are `DecodedEventProcessor`, which receives
information decoded from the raw event stream but otherwise uninterpreted;
`PixelPhotonProcessor`, which receives photon events associated with pixel
locations in a FLIM image; and `HistogramProcessor`, which receives
frame-by-frame FLIM histogram images.

Input to `DecodedEventProcessor` is produced by a `DeviceEventDecoder` object,
examples of which are the concrete classes for Becker & Hickl and PicoQuant
event data.

The only concrete `DecodedEventProcessor` currently is `LineClockPixellator`,
which uses line markers (together with necessary parameters) to assign photons
to pixel locations, and to delimit frames in a multi-frame acquisition.

The example program `SPCToHistogram` exercises the above classes to read a
Becker & Hickl `.spc` file containing raw event data and produce a cumulative
FLIM histogram.


Performance
-----------

It should be noted that FLIMEvents benefits greatly from compiler optimization.
With Visual C++, an improvement of over 50-fold was observed between Debug
(`/MDd /Od /RTC1`) and Release (`/MD /O2 /Oi /GL`). For live histogramming
during acquisition, an optimized build may be important. (Note that the Meson
build uses different flags by default.)


Next steps and future plans
---------------------------

- FLIMEvents classes should be placed in namespaces
- Multi-channel histograms
- Pixel-clock-based pixel assignment
- Support for Photon-HDF5 format (once it supports markers)
- Histograms could be streamed (based on completion of each pixel)


Non-goals
---------

- FLIMEvents is about photon timestamps and histograms, not file I/O and
  metadata handling. Supporting reading/writing of vendor-specific file formats
  (such as `.spc`, `.sdt`, `.ptu`, `.phu`) is out of scope. A separate library
  should handle that.
