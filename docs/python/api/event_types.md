<!--
This file is part of libtcspc
Copyright 2019-2026 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# Event types

Event classes describe, statically, the records that flow through a processing
graph at run time.

Instances of these classes describe the event *type*; they are not themselves
instances of the run-time events.

Some event types (those carrying timestamp and related data) are parameterized
by the exact numeric types used. {py:class}`~libtcspc.NumericTraits` objects
are used to specify these types.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.EventType
   libtcspc.BucketEvent
   libtcspc.BHSPCEvent
   libtcspc.DataLostEvent
   libtcspc.MarkerEvent
   libtcspc.TimeCorrelatedDetectionEvent
   libtcspc.TimeReachedEvent
   libtcspc.WarningEvent
```
