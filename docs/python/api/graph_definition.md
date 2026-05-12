<!--
This file is part of libtcspc
Copyright 2019-2026 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# Graph definition

A processing graph ({py:class}`~libtcspc.Graph`) is a directed acyclic graph of
nodes ({py:class}`~libtcspc.Node`) describing how event streams flow and are
transformed. Most nodes are also known as processors (the exception is
{py:class}`~libtcspc.Subgraph`).

The processing graph is a pure-Python data structure that describes the desired
processing: it does not execute anything by itself.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Graph
   libtcspc.Node
   libtcspc.Subgraph
```

```{toctree}
---
maxdepth: 1
---
event_types
processor_nodes
auxiliary_objects
```
