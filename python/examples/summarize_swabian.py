# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import argparse
import sys

import libtcspc as tcspc
import numpy as np

RECORD_COUNT_TAG = tcspc.AccessTag("counter")
TIMES_TAG = tcspc.AccessTag("times")
CHANNELS_TAG = tcspc.AccessTag("channels")
HIST_TAG = tcspc.AccessTag("hist")

numtraits = tcspc.NumericTraits()

# Numeric traits for the summary histogram: a wide bin type so per-channel
# counts cannot overflow.
summary_traits = tcspc.NumericTraits(bin_type=np.uint64)

# Trivial event used to inject a reset (to conclude the histogram) on flush.
reset = tcspc.CustomEvent("reset_event")

MAX_BIN_INDEX = 255


def build_graph() -> tcspc.Graph:
    g = tcspc.Graph()
    g.add_chain(
        [
            tcspc.read_events_from_binary_file(
                tcspc.SwabianTagEvent(),
                tcspc.Param("filename"),
                stop_normally_on_error=True,
            ),
            tcspc.Count(tcspc.SwabianTagEvent(), RECORD_COUNT_TAG),
            tcspc.DecodeSwabianTags(numtraits),
            tcspc.CheckMonotonic(numtraits),
            tcspc.Stop(
                (
                    tcspc.WarningEvent(),
                    tcspc.BeginLostIntervalEvent(numtraits),
                    tcspc.EndLostIntervalEvent(numtraits),
                    tcspc.LostCountsEvent(numtraits),
                ),
                "error",
            ),
            tcspc.RecordAbstimeRange(TIMES_TAG, numtraits),
            tcspc.MapToDatapoints(
                tcspc.DetectionEvent(numtraits),
                tcspc.ChannelDataMapper(summary_traits),
                summary_traits,
            ),
            tcspc.MapToBins(
                tcspc.UniqueBinMapper(
                    access_tag=CHANNELS_TAG,
                    max_bin_index=MAX_BIN_INDEX,
                )
            ),
            tcspc.Append(reset.value()),
            tcspc.Histogram(
                MAX_BIN_INDEX + 1,
                2**64 - 1,
                reset,
                emit_concluding=True,
                numeric_traits=summary_traits,
            ),
            tcspc.RecordLast(tcspc.ConcludingHistogramEvent(summary_traits), HIST_TAG),
            tcspc.SinkAll(),
        ]
    )
    return g


def summarize(filename: str) -> int:
    print("Compiling processing graph...", file=sys.stderr)
    g = build_graph()
    cg = tcspc.CompiledGraph(g)
    ctx = tcspc.ExecutionContext(cg, {"filename": filename})

    def print_summary() -> None:
        first = ctx.access(TIMES_TAG).min()
        if first is None:
            print("No events")
            return
        print(f"Time of first event: \t{first}")
        print(f"Time of last event: \t{ctx.access(TIMES_TAG).max()}")
        channels = ctx.access(CHANNELS_TAG).values()
        hist = ctx.access(HIST_TAG).get()
        counts = []
        if hist is not None:
            data = hist.data_bucket
            n = min(len(channels), len(data))
            for i in range(n):
                counts.append((channels[i], int(data[i])))
        counts.sort()
        for chnum, chcnt in counts:
            print(f"{chnum}: \t{chcnt}")

    try:
        ctx.flush()
        print_summary()
    except tcspc.EndOfProcessing as e:
        print_summary()
        print(str(e), file=sys.stderr)
        print("The above results are up to the error", file=sys.stderr)
    except Exception as e:
        print(str(e), file=sys.stderr)
        return 1

    print(
        f"{ctx.access(RECORD_COUNT_TAG).count()} records decoded",
        file=sys.stderr,
    )
    return 0


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
        ctx = tcspc.ExecutionContext(cg, {"filename": args.filename})
        print(ctx.cpp_to_graphviz())
        return 0

    if args.filename is None:
        print("filename is required", file=sys.stderr)
        return 2
    return summarize(args.filename)


if __name__ == "__main__":
    sys.exit(main())
