# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

"""Compute per-pixel FLIM histograms from a raw Becker-Hickl SPC file.

Marker 0 in the SPC file is treated as the pixel clock (start of each pixel).
There must be no other marker-0 events.

The output is a raw, little-endian ``uint16`` byte stream of histogram counts,
byte-compatible with the equivalent C++ example
(``examples/flim_bruker_bh_spc.cpp``). It can be read with
``numpy.fromfile(output_file, dtype=numpy.uint16)`` and reshaped to
``(frame_count, height, width, 256)`` (or ``(height, width, 256)`` with
``--sum``). The last axis is the time-difference histogram (reduced to 8 bits).
An incomplete frame at the end of the input is discarded.
"""

import argparse
import os
import sys
from typing import Any

import libtcspc as tcspc
import numpy as np

RECORD_COUNT_TAG = tcspc.AccessTag("record_counter")
PIXEL_COUNT_TAG = tcspc.AccessTag("pixel_counter")
FRAME_COUNT_TAG = tcspc.AccessTag("frame_counter")

# BH SPC has no negative channel or marker numbers.
numtraits = tcspc.NumericTraits(channel_type=np.uint32)


class _BinFileSink(tcspc.PySink):
    def __init__(self, file: Any) -> None:
        self._file = file

    def handle(self, event: Any) -> None:
        # Buckets are delivered as a contiguous 1-D NumPy view.
        self._file.write(event.tobytes())

    def flush(self) -> None:
        pass


def _histogram_terminus(
    cumulative: bool,
    num_elements: int,
    reset: tcspc.CustomEvent,
) -> list[tcspc.Node]:
    if cumulative:
        return [
            tcspc.Append(reset.value()),
            tcspc.ScanHistograms(
                num_elements=num_elements,
                num_bins=256,
                max_per_bin=65535,
                reset_event_type=reset,
                emit_concluding=True,
                numeric_traits=numtraits,
            ),
            tcspc.Count(tcspc.HistogramArrayEvent(numtraits), FRAME_COUNT_TAG),
            tcspc.Select(tcspc.ConcludingHistogramArrayEvent(numtraits)),
            tcspc.ExtractBucket(
                tcspc.ConcludingHistogramArrayEvent(numtraits)
            ),
        ]
    return [
        tcspc.ScanHistograms(
            num_elements=num_elements,
            num_bins=256,
            max_per_bin=65535,
            clear_every_scan=True,
            numeric_traits=numtraits,
        ),
        tcspc.Select(tcspc.HistogramArrayEvent(numtraits)),
        tcspc.Count(tcspc.HistogramArrayEvent(numtraits), FRAME_COUNT_TAG),
        tcspc.ExtractBucket(tcspc.HistogramArrayEvent(numtraits)),
    ]


def build_graph(
    channel: int, width: int, height: int, cumulative: bool
) -> tcspc.Graph:
    pixel_start = tcspc.CustomEvent(
        "pixel_start_event", abstime=True, traits=numtraits
    )
    pixel_stop = tcspc.CustomEvent(
        "pixel_stop_event", abstime=True, traits=numtraits
    )
    reset = tcspc.CustomEvent("reset_event")

    g = tcspc.Graph()
    g.add_chain(
        [
            tcspc.read_events_from_binary_file(
                tcspc.BHSPCEvent(),
                tcspc.Param("filename"),
                start_offset=4,
            ),
            tcspc.Count(tcspc.BHSPCEvent(), RECORD_COUNT_TAG),
            tcspc.DecodeBHSPC(numtraits),
            tcspc.CheckMonotonic(numtraits),
            tcspc.StopWithError(
                (tcspc.WarningEvent(), tcspc.DataLostEvent(numtraits)),
                "error in input data",
            ),
            tcspc.Match(
                tcspc.MarkerEvent(numtraits),
                pixel_start,
                tcspc.ChannelMatcher(0, numtraits),
            ),
            tcspc.SelectExcept(tcspc.MarkerEvent(numtraits)),
            tcspc.Generate(
                pixel_start,
                pixel_stop,
                tcspc.OneShotTimingGenerator(
                    tcspc.Param("pixel_time"), numtraits
                ),
            ),
            tcspc.SelectExcept(tcspc.TimeReachedEvent(numtraits)),
            tcspc.CheckAlternating(pixel_start, pixel_stop),
            tcspc.StopWithError(
                (tcspc.WarningEvent(), tcspc.DataLostEvent(numtraits)),
                "pixel time is such that pixel stop occurs after next pixel start",
            ),
            ("count_pixels", tcspc.Count(pixel_stop, PIXEL_COUNT_TAG)),
        ]
    )
    # Route is multi-output (even with outputs=1, its output port is named
    # "output-0"), so it cannot be a chain node.
    g.add_node(
        "route",
        tcspc.Route(
            tcspc.TimeCorrelatedDetectionEvent(numtraits),
            broadcast_event_types=(pixel_start, pixel_stop),
            router=tcspc.ChannelRouter({channel: 0}, numeric_traits=numtraits),
            outputs=1,
        ),
        upstream="count_pixels",
    )
    g.add_chain(
        [
            tcspc.MapToDatapoints(
                tcspc.TimeCorrelatedDetectionEvent(numtraits),
                tcspc.DifftimeDataMapper(numtraits),
                numtraits,
            ),
            tcspc.MapToBins(
                tcspc.PowerOf2BinMapper(
                    n_data_bits=12,
                    n_histo_bits=8,
                    flip=True,
                    numeric_traits=numtraits,
                ),
                numtraits,
            ),
            tcspc.ClusterBinIncrements(pixel_start, pixel_stop, numtraits),
            *_histogram_terminus(cumulative, width * height, reset),
        ],
        upstream=("route", "output-0"),
    )
    return g


def _positive_int(s: str) -> int:
    v = int(s)
    if v <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return v


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--channel", type=int, default=0, help="select channel (default: 0)"
    )
    p.add_argument(
        "--pixel-time",
        type=_positive_int,
        default=None,
        help="pixel time in macrotime units",
    )
    p.add_argument(
        "--width",
        type=_positive_int,
        required=True,
        help="pixels per line",
    )
    p.add_argument(
        "--height",
        type=_positive_int,
        required=True,
        help="lines per frame",
    )
    p.add_argument(
        "--sum",
        action="store_true",
        help="output only the cumulative total of all complete frames",
    )
    p.add_argument(
        "--overwrite",
        action="store_true",
        help="overwrite output_file if it exists",
    )
    p.add_argument(
        "--dump-graph",
        action="store_true",
        help="do not process input; instead emit the processing graph to "
        "standard output in Graphviz dot format",
    )
    p.add_argument("input_file", nargs="?", default=None)
    p.add_argument("output_file", nargs="?", default=None)
    return p.parse_args(argv)


def _unlink_if_empty(path: str) -> None:
    # Don't leave empty output file if nothing was written
    try:
        if os.path.getsize(path) == 0:
            os.unlink(path)
    except OSError:
        pass


def run(args: argparse.Namespace) -> int:
    if not args.dump_graph and (
        args.pixel_time is None
        or args.input_file is None
        or args.output_file is None
    ):
        print(
            "--pixel-time, input_file, and output_file are required",
            file=sys.stderr,
        )
        return 2

    if args.dump_graph:
        g = build_graph(args.channel, args.width, args.height, args.sum)
        print(g.to_graphviz())
        return 0

    print("Creating processing graph...", file=sys.stderr)
    g = build_graph(args.channel, args.width, args.height, args.sum)

    print("Compiling processing graph...", file=sys.stderr)
    cg = tcspc.CompiledGraph(g)

    mode = "wb" if args.overwrite else "xb"
    try:
        out_file = open(args.output_file, mode)  # noqa: SIM115
    except FileExistsError:
        print(
            f"output file exists (use --overwrite): {args.output_file}",
            file=sys.stderr,
        )
        return 1
    except OSError as e:
        print(e, file=sys.stderr)
        return 2

    ctx: tcspc.ExecutionContext | None = None
    try:
        with out_file:
            ctx = tcspc.ExecutionContext(
                cg,
                {
                    "filename": args.input_file,
                    "pixel_time": args.pixel_time,
                },
                (_BinFileSink(out_file),),
            )

            print("Processing...", file=sys.stderr)
            try:
                ctx.flush()
            except tcspc.EndOfProcessing as e:
                print(f"Stopped because: {e}", file=sys.stderr)
    except Exception as e:
        print(e, file=sys.stderr)
        _unlink_if_empty(args.output_file)
        return 2

    _unlink_if_empty(args.output_file)
    assert ctx is not None

    pixels_per_frame = args.width * args.height
    records = ctx.access(RECORD_COUNT_TAG).count()
    pixels = ctx.access(PIXEL_COUNT_TAG).count()
    frames = ctx.access(FRAME_COUNT_TAG).count()
    print(f"records decoded: {records}")
    print(f"pixels finished: {pixels}")
    print(f"pixels per frame: {pixels_per_frame}")
    print(f"frames finished: {frames}")
    print(
        f"discarded pixels in incomplete frame: "
        f"{pixels - frames * pixels_per_frame}"
    )
    return 0


def main() -> int:
    return run(parse_args())


if __name__ == "__main__":
    sys.exit(main())
