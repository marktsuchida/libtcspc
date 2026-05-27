<!--
This file is part of libtcspc
Copyright 2019-2026 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# Execution

Run a compiled graph inside an execution context, supply runtime components
defined in Python, and access live state from a running graph.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.ExecutionContext
   libtcspc.EndOfProcessing
```

## Run-time Python components

User-supplied Python objects passed to {py:class}`~libtcspc.ExecutionContext`
to handle input to and output from the graph.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.PySink
   libtcspc.PyAcquisitionReader
   libtcspc.PyBucketSource
```

## Runtime access

Accessor objects expose live state from processors in running graphs. Use an
{py:class}`~libtcspc.AccessTag` declared at graph-definition time to look up
the corresponding accessor from {py:class}`~libtcspc.ExecutionContext`. The
accessor has a run-time-generated type but implements one of the
{py:class}`~libtcspc.Accessor` protocols.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Accessor
   libtcspc.AcquireAccessor
   libtcspc.CountAccessor
   libtcspc.RecordAbstimeRangeAccessor
   libtcspc.RecordLastAccessor
   libtcspc.UniqueBinMapperAccessor
```
