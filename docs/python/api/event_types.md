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

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.EventType
   libtcspc.EventInstance
   libtcspc.BucketEvent
   libtcspc.ConstBucketEvent
   libtcspc.BHSPCEvent
   libtcspc.BinIncrementClusterEvent
   libtcspc.BinIncrementEvent
   libtcspc.ConcludingHistogramArrayEvent
   libtcspc.ConcludingHistogramEvent
   libtcspc.DataLostEvent
   libtcspc.DatapointEvent
   libtcspc.DetectionPairEvent
   libtcspc.HistogramArrayEvent
   libtcspc.HistogramArrayProgressEvent
   libtcspc.HistogramEvent
   libtcspc.MarkerEvent
   libtcspc.PeriodicSequenceModelEvent
   libtcspc.RealLinearTimingEvent
   libtcspc.RealOneShotTimingEvent
   libtcspc.TimeCorrelatedDetectionEvent
   libtcspc.TimeReachedEvent
   libtcspc.WarningEvent
```
