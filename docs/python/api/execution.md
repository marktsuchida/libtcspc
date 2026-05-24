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

Access objects expose live state from processors in running graphs. Use an
{py:class}`~libtcspc.AccessTag` declared at graph-definition time to look up
the corresponding access object from {py:class}`~libtcspc.ExecutionContext`.
The access object has a run-time-generated type but implements one of the
{py:class}`~libtcspc.Access` protocols.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Access
   libtcspc.AcquireAccess
   libtcspc.CountAccess
   libtcspc.UniqueBinMapperAccess
```
