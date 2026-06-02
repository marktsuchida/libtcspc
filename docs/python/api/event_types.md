<!--
This file is part of libtcspc
Copyright 2019-2026 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# Event types

Event classes describe, statically, the records that flow through a processing
graph at run time.

Instances of these classes describe the event *type*; they are not themselves
instances of the run-time events. An {py:class}`~libtcspc.EventInstance`, built
via {py:meth}`EventType.value() <libtcspc.EventType.value>`, *is* a concrete
event value; it is used to insert events into a stream with
{py:class}`~libtcspc.Prepend` and {py:class}`~libtcspc.Append`.

Some event types (those carrying timestamp and related data) are parameterized
by the exact numeric types used. {py:class}`~libtcspc.NumericTraits` objects
are used to specify these types.

## Generic and core event types

Base, value, and container event types, and generic events used throughout the
library.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.EventType
   libtcspc.EventInstance
   libtcspc.CustomEvent
   libtcspc.BucketEvent
   libtcspc.ConstBucketEvent
   libtcspc.VariantEvent
   libtcspc.WarningEvent
```

## Device event types

Event types representing the raw records produced by specific TCSPC hardware.

### Becker & Hickl device event types

Raw event records from Becker & Hickl devices.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.BHSPCEvent
   libtcspc.BHSPC600_256chEvent
   libtcspc.BHSPC600_4096chEvent
```

### PicoQuant device event types

Raw event records from PicoQuant devices.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.PQT2PicoHarp300Event
   libtcspc.PQT2HydraHarpV1Event
   libtcspc.PQT2GenericEvent
   libtcspc.PQT3PicoHarp300Event
   libtcspc.PQT3HydraHarpV1Event
   libtcspc.PQT3GenericEvent
```

### Swabian device event types

Raw event records from Swabian Instruments devices.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.SwabianTagEvent
```

## Time tag and TCSPC event types

Decoded time tag and TCSPC event types used throughout processing.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.TimeReachedEvent
   libtcspc.BulkCountsEvent
   libtcspc.DetectionEvent
   libtcspc.TimeCorrelatedDetectionEvent
   libtcspc.MarkerEvent
   libtcspc.DetectionPairEvent
```

### Lost data event types

Event types signaling lost or dropped data.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.DataLostEvent
   libtcspc.BeginLostIntervalEvent
   libtcspc.EndLostIntervalEvent
   libtcspc.LostCountsEvent
```

## Timing modeling event types

Event types describing modeled timing sequences.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.PeriodicSequenceModelEvent
   libtcspc.RealOneShotTimingEvent
   libtcspc.RealLinearTimingEvent
```

## Binning event types

Event types used in datapoint and bin-increment processing.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.DatapointEvent
   libtcspc.BinIncrementEvent
   libtcspc.BinIncrementClusterEvent
```

## Histogram event types

Event types carrying histograms and histogram arrays.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.HistogramEvent
   libtcspc.ConcludingHistogramEvent
   libtcspc.HistogramArrayEvent
   libtcspc.HistogramArrayProgressEvent
   libtcspc.ConcludingHistogramArrayEvent
```
