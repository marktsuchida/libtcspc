<!--
This file is part of libtcspc
Copyright 2019-2026 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# Processor nodes

Built-in processor nodes, grouped by category.

## Core processors

Basic and generic processors.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.SinkAll
   libtcspc.SourceNothing
   libtcspc.Prepend
   libtcspc.Append
```

## Filtering processors

Processors for filtering events.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Gate
   libtcspc.Select
   libtcspc.SelectAll
   libtcspc.SelectExcept
   libtcspc.SelectNone
```

## Batching and unbatching processors

Processors that aggregate events into batches or extract individual events from
batches.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Batch
   libtcspc.BatchBinIncrementClusters
   libtcspc.Unbatch
   libtcspc.UnbatchBinIncrementClusters
```

## Buffering processors

Processors that buffer events to decouple upstream and downstream processing.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.ProcessInBatches
```

## Stopping processors

Processors that stop processing when a given event is received.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Stop
   libtcspc.StopWithError
```

## Branching processors

Processors for splitting the processing graph.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Broadcast
   libtcspc.Route
```

## Merging processors

Processors for combining several branches of the processing graph.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Merge
   libtcspc.MergeN
   libtcspc.MergeNUnsorted
```

## Input and output processors

Processors for reading and writing data from/to file-like streams.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.ExtractBucket
   libtcspc.ReadBinaryStream
   libtcspc.WriteBinaryStream
   libtcspc.read_events_from_binary_file
   libtcspc.write_events_to_binary_file
```

### Binary stream processors

Processors for converting between events and binary data streams.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.BatchFromBytes
   libtcspc.UnbatchFromBytes
   libtcspc.ViewAsBytes
```

## Acquisition processors

Processors for acquiring data from hardware devices.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Acquire
   libtcspc.AcquireFullBuckets
   libtcspc.CopyToBuckets
   libtcspc.CopyToFullBuckets
```

## Decoding processors

Processors for decoding device events.

### Becker & Hickl decoding processors

Processors for decoding Becker & Hickl SPC device events.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.DecodeBHSPC
   libtcspc.DecodeBHSPCWithIntensityCounter
   libtcspc.DecodeBHSPC600_256ch
   libtcspc.DecodeBHSPC600_4096ch
```

### PicoQuant decoding processors

Processors for decoding PicoQuant T2 and T3 device events.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.DecodePQT2Generic
   libtcspc.DecodePQT2PicoHarp300
   libtcspc.DecodePQT2HydraHarpV1
   libtcspc.DecodePQT3Generic
   libtcspc.DecodePQT3PicoHarp300
   libtcspc.DecodePQT3HydraHarpV1
```

### Swabian decoding processors

Processors for decoding Swabian Time Tagger device events.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.DecodeSwabianTags
```

## Timeline processors

Processors for managing and manipulating the absolute timeline.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Delay
   libtcspc.RebaseAbstime
   libtcspc.RegulateTimeReached
```

## Timing signal processors

Processors for transforming timing signal events.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.CountDownTo
   libtcspc.CountUpTo
   libtcspc.Generate
   libtcspc.Match
   libtcspc.MatchAndConsume
```

## Timing signal modeling processors

Processors for fitting and synthesizing periodic timing sequences.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.AddCountToPeriodicSequences
   libtcspc.ConvertSequencesToStartStop
   libtcspc.ExtrapolatePeriodicSequences
   libtcspc.FitPeriodicSequences
   libtcspc.RetimePeriodicSequences
```

## Time correlation processors

Processors for time correlation.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.RecoverOrder
   libtcspc.RemoveTimeCorrelation
   libtcspc.TimeCorrelateAtFraction
   libtcspc.TimeCorrelateAtMidpoint
   libtcspc.TimeCorrelateAtStart
   libtcspc.TimeCorrelateAtStop
```

## Pairing processors

Processors for pairing detection events.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.PairAll
   libtcspc.PairAllBetween
   libtcspc.PairOne
   libtcspc.PairOneBetween
```

## Binning processors

Processors for mapping events to datapoints and histogram bins.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.ClusterBinIncrements
   libtcspc.MapToBins
   libtcspc.MapToDatapoints
```

## Histogramming processors

Processors for accumulating histograms.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Histogram
   libtcspc.ScanHistograms
```

## Validation processors

Processors for data validation.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.CheckAlternating
   libtcspc.CheckMonotonic
```

## Statistics processors

Processors for collecting statistics.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.Count
```

## Testing processors

Processors for unit testing of processors.

```{eval-rst}
.. autosummary::
   :toctree: generated
   :nosignatures:

   libtcspc.SinkOnly
```
