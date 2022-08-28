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

Everything is in the `flimevents` namespace.

### Events

Events represent the data that is passed down the processing pipelines.

Some of these form class hierarchies, but only to model common fields. They do
not have virtual methods, and the inheritance relationship is not significant
in how they are processed.

Event classes are pure data and have public data members.

Raw device envents:

- These can be `memcpy()`ed from the raw data stream events
- Becker-Hickl: `BHSPCEvent`, `BHSPC600Event48`, `BHSPC600Event32`
  - Event sets `BHSPCEvents`, `BHSPC600Events48`, `BHSPC600Events32`
- PicoQuant: `PicoT3Event`, `HydraV1T3Event`, `HydraV2T3Event`
  - Event sets `PQT3Events`, `PQHydraV1T3Events`, `PQHydraV2T3Events`

TCSPC events:

- Abstract: `TCSPCEvent`, `BasePhotonEvent`
- Concrete: `ValidPhotonEvent`, `InvalidPhotonEvent`, `DataLostEvent`,
  `MarkerEvent`, `TimestampEvent`
- Event set `TCSPCEvents`

Pixel-assigned photon events:

- `PixelPhotonEvent`, `BeginFrameEvent`, `EndFrameEvent`
- Event set `PixelPhotonEvents`

Histogram events:

- `FrameHistogramEvent`, `IncompleteFrameHistogramEvent`,
  `FinalCumulativeHistogramEvent`
- Event sets `FrameHistogramEvents`, `CumulativeHistogramEvents`

Generic:

- `EventArray<E>`

### Processors

Processors are just classes with the following member functions:

- `void HandleEvent(E const& event) noexcept` -- a statically polymorphic
  (i.e., overloaded for different event types) function, for each event in the
  event set that is processed by the processor
- `void HandleEnd(std::exception_ptr error) noexcept` -- `error` is the null
  value (default-constructed `exception_ptr`) when the event stream ends
  successfully

The last argument to the constructor of a processor is usually `D&&
downstream`, a reference to the downstream processor that will handle the
events emitted by this processor. The downstream processor is moved into the
processor.

Raw device events to TCSPC events:

- `DecodeBHSPC`, `DecodeBHSPC600_48`, `DecodeBHSPC600_32`

TCSPC events to pixel photon events:

- `LineClockPixellator`

Pixel photon events to pixel photon events:

- `Histogrammer`, `SequentialHistogrammer`

Frame histogram events to cumulative histogram events:

- `HistogramAccumulator`

Generic processors:

- `Broadcast` (`E` to `E`), `EventArrayDemultiplexer` (`EventArray<E>` to `E`),
  `EventBuffer` (`E` to `EventArray<E>`)

### Upstream buffering

Usually it is a good idea to buffer incoming events (from hardware) before
processing, so as not to cause a hardware buffer overflow when the incoming
event rate temporarily exceeds the processing rate (processing may include slow
or high-jitter tasks such as writing to disk). For this, `EventBuffer` is used.

It is just a processor that bunches a given event into the corresponding
`EventArray` event, except that it internally buffers the event arrays and only
emits them when its `void PumpDownstream() noexcept` member function is called.

All other processors do not buffer events (except when needed for the
processing itself), and processing completes synchronously during the
`PumpDownstream` call. Thus, `PumpDownstream` should be called on a different
thread from the thread reading from the device and sending them to
`EventBuffer`.
