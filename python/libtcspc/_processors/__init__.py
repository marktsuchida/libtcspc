# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from ._acquisition import (
    Acquire,
    AcquireFullBuckets,
    CopyToBuckets,
    CopyToFullBuckets,
)
from ._batching import (
    Batch,
    BatchBinIncrementClusters,
    Unbatch,
    UnbatchBinIncrementClusters,
)
from ._binning import ClusterBinIncrements, MapToBins, MapToDatapoints
from ._branching import Broadcast, Route
from ._buffering import Buffer, ProcessInBatches, RealTimeBuffer
from ._core import Append, Prepend, SinkAll, SourceNothing
from ._decoding import (
    DecodeBHSPC,
    DecodeBHSPC600_256ch,
    DecodeBHSPC600_4096ch,
    DecodeBHSPCWithIntensityCounter,
    DecodePQT2Generic,
    DecodePQT2HydraHarpV1,
    DecodePQT2PicoHarp300,
    DecodePQT3Generic,
    DecodePQT3HydraHarpV1,
    DecodePQT3PicoHarp300,
    DecodeSwabianTags,
)
from ._filtering import Gate, Select, SelectAll, SelectExcept, SelectNone
from ._histogramming import Histogram, ScanHistograms
from ._input_output import (
    BatchFromBytes,
    ExtractBucket,
    ReadBinaryStream,
    UnbatchFromBytes,
    ViewAsBytes,
    WriteBinaryStream,
    read_events_from_binary_file,
    write_events_to_binary_file,
)
from ._merging import Merge, MergeN, MergeNUnsorted
from ._mux import Demultiplex, Multiplex
from ._pairing import PairAll, PairAllBetween, PairOne, PairOneBetween
from ._statistics import Count, RecordAbstimeRange, RecordLast
from ._stopping import Stop, StopWithError
from ._testing import SinkOnly
from ._time_correlation import (
    RecoverOrder,
    RemoveTimeCorrelation,
    TimeCorrelateAtFraction,
    TimeCorrelateAtMidpoint,
    TimeCorrelateAtStart,
    TimeCorrelateAtStop,
)
from ._timeline import Delay, RebaseAbstime, RegulateTimeReached
from ._timing import CountDownTo, CountUpTo, Generate, Match, MatchAndConsume
from ._timing_modeling import (
    AddCountToPeriodicSequences,
    ConvertSequencesToStartStop,
    ExtrapolatePeriodicSequences,
    FitPeriodicSequences,
    RetimePeriodicSequences,
)
from ._validation import CheckAlternating, CheckMonotonic

__all__ = [
    "Acquire",
    "AcquireFullBuckets",
    "AddCountToPeriodicSequences",
    "Append",
    "Batch",
    "BatchBinIncrementClusters",
    "BatchFromBytes",
    "Broadcast",
    "Buffer",
    "CheckAlternating",
    "CheckMonotonic",
    "ClusterBinIncrements",
    "ConvertSequencesToStartStop",
    "CopyToBuckets",
    "CopyToFullBuckets",
    "Count",
    "CountDownTo",
    "CountUpTo",
    "DecodeBHSPC",
    "DecodeBHSPC600_256ch",
    "DecodeBHSPC600_4096ch",
    "DecodeBHSPCWithIntensityCounter",
    "DecodePQT2Generic",
    "DecodePQT2HydraHarpV1",
    "DecodePQT2PicoHarp300",
    "DecodePQT3Generic",
    "DecodePQT3HydraHarpV1",
    "DecodePQT3PicoHarp300",
    "DecodeSwabianTags",
    "Delay",
    "Demultiplex",
    "ExtractBucket",
    "ExtrapolatePeriodicSequences",
    "FitPeriodicSequences",
    "Gate",
    "Generate",
    "Histogram",
    "MapToBins",
    "MapToDatapoints",
    "Match",
    "MatchAndConsume",
    "Merge",
    "MergeN",
    "MergeNUnsorted",
    "Multiplex",
    "PairAll",
    "PairAllBetween",
    "PairOne",
    "PairOneBetween",
    "Prepend",
    "ProcessInBatches",
    "ReadBinaryStream",
    "RealTimeBuffer",
    "RebaseAbstime",
    "RecordAbstimeRange",
    "RecordLast",
    "RecoverOrder",
    "RegulateTimeReached",
    "RemoveTimeCorrelation",
    "RetimePeriodicSequences",
    "Route",
    "ScanHistograms",
    "Select",
    "SelectAll",
    "SelectExcept",
    "SelectNone",
    "SinkAll",
    "SinkOnly",
    "SourceNothing",
    "Stop",
    "StopWithError",
    "TimeCorrelateAtFraction",
    "TimeCorrelateAtMidpoint",
    "TimeCorrelateAtStart",
    "TimeCorrelateAtStop",
    "Unbatch",
    "UnbatchBinIncrementClusters",
    "UnbatchFromBytes",
    "ViewAsBytes",
    "WriteBinaryStream",
    "read_events_from_binary_file",
    "write_events_to_binary_file",
]
