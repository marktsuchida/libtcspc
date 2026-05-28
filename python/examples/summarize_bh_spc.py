# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import argparse
import sys
from typing import Any

import libtcspc as tcspc
import numpy as np

RECORD_COUNT_TAG = tcspc.AccessTag("count-records")
TIMES_TAG = tcspc.AccessTag("times")

# BH SPC has no negative channel or marker numbers.
numtraits = tcspc.NumericTraits(channel_type=np.uint32)

# Numeric traits for the summary histograms: a wide bin type so counts cannot
# overflow, and a datapoint type wide enough to hold the channel.
summary_traits = tcspc.NumericTraits(
    channel_type=np.uint32, datapoint_type=np.uint32, bin_type=np.uint64
)

# Trivial event used to inject a reset (to conclude the histograms) on flush.
reset = tcspc.CustomEvent("reset_event")


class _LastBucketSink(tcspc.PySink):
    # Stores a copy of the last bucket received as a NumPy array. Buckets are
    # delivered as a zero-copy view, so the copy is required to keep the data
    # past the handle() call.
    def __init__(self) -> None:
        self.last: np.ndarray | None = None

    def handle(self, event: Any) -> None:
        self.last = np.copy(event)

    def flush(self) -> None:
        pass


def histogram_branch(
    event_type: tcspc.EventType, num_bins: int, max_bin_index: int
) -> list[tcspc.Node]:
    # One broadcast branch: map the event's channel to a datapoint, bin it
    # one-to-one, and accumulate a histogram concluded on flush (via the
    # appended reset). The concluded bins are extracted as a NumPy bucket.
    return [
        tcspc.MapToDatapoints(
            event_type,
            tcspc.ChannelDataMapper(summary_traits),
            summary_traits,
        ),
        tcspc.MapToBins(
            tcspc.LinearBinMapper(
                0, 1, max_bin_index, numeric_traits=summary_traits
            ),
            summary_traits,
        ),
        tcspc.Append(reset.value()),
        tcspc.Histogram(
            num_bins,
            2**64 - 1,
            reset,
            emit_concluding=True,
            numeric_traits=summary_traits,
        ),
        tcspc.Select(tcspc.ConcludingHistogramEvent(summary_traits)),
        tcspc.ExtractBucket(tcspc.ConcludingHistogramEvent(summary_traits)),
    ]


def build_graph() -> tcspc.Graph:
    g = tcspc.Graph()
    g.add_chain(
        [
            tcspc.read_events_from_binary_file(
                tcspc.BHSPCEvent(),
                tcspc.Param("filename"),
                start_offset=4,
                stop_normally_on_error=True,
            ),
            tcspc.Count(tcspc.BHSPCEvent(), RECORD_COUNT_TAG),
            tcspc.DecodeBHSPC(numtraits),
            tcspc.CheckMonotonic(numtraits),
            tcspc.Stop(
                (tcspc.WarningEvent(), tcspc.DataLostEvent(numtraits)),
                "error in data",
            ),
            ("times", tcspc.RecordAbstimeRange(TIMES_TAG, numtraits)),
        ]
    )
    # Broadcast has two outputs, so it cannot be the last node of add_chain.
    g.add_node(
        "bcast",
        tcspc.Broadcast(
            tcspc.TimeCorrelatedDetectionEvent(numtraits),
            tcspc.MarkerEvent(numtraits),
            tcspc.TimeReachedEvent(numtraits),
            outputs=2,
        ),
        upstream="times",
    )
    # The two unconnected branch outputs become the graph's output ports and
    # map positionally to the downstreams tuple, in the order added: photons
    # first, then markers.
    g.add_chain(
        histogram_branch(
            tcspc.TimeCorrelatedDetectionEvent(numtraits), 16, 15
        ),
        upstream=("bcast", "output-0"),
    )
    g.add_chain(
        histogram_branch(tcspc.MarkerEvent(numtraits), 4, 3),
        upstream=("bcast", "output-1"),
    )
    return g


def summarize(filename: str) -> int:
    print("Creating processing graph...", file=sys.stderr)
    g = build_graph()

    ret = 0
    print("Compiling processing graph...", file=sys.stderr)
    cg = tcspc.CompiledGraph(g)
    photon_sink, marker_sink = _LastBucketSink(), _LastBucketSink()
    ctx = tcspc.ExecutionContext(
        cg, {"filename": filename}, (photon_sink, marker_sink)
    )

    print("Processing...", file=sys.stderr)
    try:
        ctx.flush()
    except tcspc.EndOfProcessing as e:
        print(f"Stopped because: {e}")
        print("The following results are up to where processing stopped.")
        ret = 1
    except Exception as e:
        print(e, file=sys.stderr)
        return 2

    last_time = ctx.access(TIMES_TAG).max()
    print(
        f"Relative time of last event: \t"
        f"{last_time if last_time is not None else '(none)'}"
    )
    for i in range(16):
        count = photon_sink.last[i] if photon_sink.last is not None else 0
        print(f"route {i}: \t{count}")
    for i in range(4):
        count = marker_sink.last[i] if marker_sink.last is not None else 0
        print(f"mark {i}: \t{count}")

    print(
        f"{ctx.access(RECORD_COUNT_TAG).count()} records decoded",
        file=sys.stderr,
    )
    return ret


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument(
        "--dump-graph",
        action="store_true",
        help="emit the processing graph to standard output in Graphviz dot "
        "format and exit; does not process input",
    )
    p.add_argument(
        "--dump-cpp-graph",
        action="store_true",
        help="emit the compiled C++ processor graph to standard output in "
        "Graphviz dot format and exit; requires a real input file (the file is "
        "opened at processor construction time but never read)",
    )
    p.add_argument("filename", nargs="?", default=None)
    args = p.parse_args()

    if args.dump_graph:
        print(build_graph().to_graphviz())
        return 0

    if args.dump_cpp_graph:
        if args.filename is None:
            print("filename is required", file=sys.stderr)
            return 2
        g = build_graph()
        cg = tcspc.CompiledGraph(g)
        ctx = tcspc.ExecutionContext(
            cg,
            {"filename": args.filename},
            (_LastBucketSink(), _LastBucketSink()),
        )
        print(ctx.cpp_to_graphviz())
        return 0

    if args.filename is None:
        print("filename is required", file=sys.stderr)
        return 2
    return summarize(args.filename)


if __name__ == "__main__":
    sys.exit(main())
