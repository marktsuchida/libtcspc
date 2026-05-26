<!--
This file is part of libtcspc
Copyright 2019-2026 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# Auxiliary objects

These objects are used during graph definition to configure event types and
processor nodes.

## Numeric traits

{py:class}`~libtcspc.NumericTraits` objects are used to specify the numeric
types used for timestamp and related event fields.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.NumericTraits
```

## Parameters

Various settings of processing nodes and other auxiliary objects can be left
parameterized in the graph, so that the compiled graph can be reused with
different values for those settings, supplied when creating the execution
context. The {py:class}`~libtcspc.Param` object serves as a placeholder for
parameterized values.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Param
```

## Access tags

Some processing nodes support direct interaction at run time. This is used, for
example, to retrieve results from nodes that collect statistics, or to control
stream behavior such as buffering. To enable such access, an
{py:class}`~libtcspc.AccessTag` is attached to the node during graph
definition; at execution time, access to the node can be obtained using the
same tag.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.AccessTag
```

## Acquisition readers

Readers that supply data to the {py:class}`~libtcspc.Acquire` processor by
wrapping pull-style device APIs.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.AcquisitionReader
   libtcspc.NullReader
   libtcspc.StuckReader
```

## Bucket sources

Allocation strategies for bucket instances used by processors that produce bulk
data.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.BucketSource
   libtcspc.NewDeleteBucketSource
   libtcspc.RecyclingBucketSource
```

## Routers

Routers select the destination output port for events handled by the
{py:class}`~libtcspc.Route` processor.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Router
   libtcspc.ChannelRouter
   libtcspc.NullRouter
```

## Matchers

Matchers decide which events are matched by the {py:class}`~libtcspc.Match` and
{py:class}`~libtcspc.MatchAndConsume` processors.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Matcher
   libtcspc.AlwaysMatcher
   libtcspc.NeverMatcher
   libtcspc.ChannelMatcher
```

## Timing generators

Timing generators produce patterns of timing events for the
{py:class}`~libtcspc.Generate` processor.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.TimingGenerator
   libtcspc.NullTimingGenerator
   libtcspc.OneShotTimingGenerator
   libtcspc.DynamicOneShotTimingGenerator
   libtcspc.LinearTimingGenerator
   libtcspc.DynamicLinearTimingGenerator
```

## Data mappers

Data mappers extract a datapoint value from events for the
{py:class}`~libtcspc.MapToDatapoints` processor.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.DataMapper
   libtcspc.DifftimeDataMapper
   libtcspc.CountDataMapper
   libtcspc.ChannelDataMapper
```

## Bin mappers

Bin mappers map datapoint values to histogram bin indices for the
{py:class}`~libtcspc.MapToBins` processor.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.BinMapper
   libtcspc.PowerOf2BinMapper
   libtcspc.LinearBinMapper
   libtcspc.UniqueBinMapper
```

## Input streams

Inputs to source processor nodes such as
{py:class}`~libtcspc.ReadBinaryStream`.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.InputStream
   libtcspc.NullInputStream
   libtcspc.BinaryFileInputStream
```

## Output streams

Outputs to sink processor nodes such as
{py:class}`~libtcspc.WriteBinaryStream`.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.OutputStream
   libtcspc.NullOutputStream
   libtcspc.BinaryFileOutputStream
```
