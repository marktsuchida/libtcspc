# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

"""
This program computes FLIM histograms from raw Swabian tag dumps (16-byte
binary records; not to be confused with Swabian .ttbin files). In addition to
the rising and falling edges of the photons, the data must contain the laser
sync signal (typically with conditional filter applied by hardware) and a pixel
marker signal indicating the pixel starts.

Photon times are computed as the midpoint between the leading and trailing
edges of the pulse. The photons are then time-correlated with the laser sync
signal, with the laser sync being the start and the photon being the stop of
the difference time measurement.

Usually acquisition should be done with the laser sync signal being
conditionally filtered in hardware, triggered by the photon signal, so it is
necessary to apply a negative delay (--sync-delay) to the laser sync.

The output is a raw binary array file of 16-bit unsigned integers. It can be
read, for example, with numpy.fromfile(output_file, dtype=numpy.uint16).

When --sum is not given, the array has the shape (in NumPy axis order)
    (frame_count, height, width, bin_count).

When --sum is given, the array has the shape (height, width, bin_count).

In all cases, if there is an incomplete frame at the end of the input, it is
excluded from the output.

To work with data produced by Bruker software, processing stops without an
error upon detection of a decreasing timestamp in the input.
"""

import argparse
import io
import os
import sys
from typing import Any

import libtcspc as tcspc

RECORD_COUNT_TAG = tcspc.AccessTag("record_counter")
PIXEL_COUNT_TAG = tcspc.AccessTag("pixel_counter")
FRAME_COUNT_TAG = tcspc.AccessTag("frame_counter")

numtraits = tcspc.NumericTraits()


class _BinFileSink(tcspc.PySink):
    """Writes binary data to a file."""

    def __init__(self, file: Any) -> None:
        self._file = file

    def handle(self, event: Any) -> None:
        # Buckets are delivered as a contiguous 1-D NumPy view.
        self._file.write(event.tobytes())

    def flush(self) -> None:
        pass


pixel_start = tcspc.CustomEvent(
    "pixel_start_event", abstime=True, traits=numtraits
)
pixel_stop = tcspc.CustomEvent(
    "pixel_stop_event", abstime=True, traits=numtraits
)


def build_graph(args: argparse.Namespace) -> tcspc.Graph:
    g = tcspc.Graph()
    g.add_chain(
        nodes=(
            _source_events(),
            _process_events(args),
            _generate_histograms(args, pixel_stop),
        )
    )
    return g


def _source_events() -> tcspc.Subgraph:
    """Subgraph responsible for reading and preprocessing input data."""
    g = tcspc.Graph()
    g.add_chain(
        nodes=(
            tcspc.read_events_from_binary_file(
                tcspc.SwabianTagEvent(),
                tcspc.Param("filename"),
            ),
            tcspc.StopWithError(
                (tcspc.WarningEvent(),), "error reading input data from file"
            ),
            tcspc.Count(tcspc.SwabianTagEvent(), RECORD_COUNT_TAG),
            tcspc.DecodeSwabianTags(numtraits),
            tcspc.StopWithError(
                (
                    tcspc.WarningEvent(),
                    tcspc.BeginLostIntervalEvent(),
                    tcspc.EndLostIntervalEvent(),
                    tcspc.LostCountsEvent(),
                ),
                "error in input data",
            ),
            tcspc.CheckMonotonic(numtraits),
            tcspc.Stop((tcspc.WarningEvent(),), "processing stopped"),
        )
    )
    return tcspc.Subgraph(
        g,
        input_map={},
        output_map={"output": g.outputs()[0]},
    )


def _process_events(args: argparse.Namespace) -> tcspc.Subgraph:
    """Processes the decoded Swabian tag events into histogram bin increments..

    Note that sync events, photon events, and pixel marker events must all be processed differently.
    This function creates a processing chain for each, including the nodes to route each event to its
    appropriate processing chain, and the nodes to merge the processed events back together.
    """

    # NB We pump TimeReachedEvents at regular intervals. Each is broadcast
    # through the Route node and are used by the Merge nodes to flush buffers.
    # The count_threshold for issuing TimeReachedEvents is chosen as 1/4 of the
    # Merge buffer size to guarantee against buffer overflows.
    merge_buffer_size = 1 << 20
    regulate_count_threshold = (
        merge_buffer_size >> 2
    )  # 1/4 of merge buffer size

    g = tcspc.Graph()
    g.add_node(
        name="regulated-source",
        node=tcspc.RegulateTimeReached(
            interval_threshold=1 << 30,  # About 1 ms
            count_threshold=regulate_count_threshold,
        ),
    )
    g.add_node(
        name="route",
        upstream="regulated-source",
        node=tcspc.Route(
            tcspc.DetectionEvent(numtraits),
            broadcast_event_types=(tcspc.TimeReachedEvent(numtraits),),
            router=tcspc.ChannelRouter(
                channel_indices={
                    args.sync_channel: 0,
                    args.photon_channels[0]: 1,
                    args.photon_channels[1]: 1,
                    args.pixel_marker_channel: 2,
                }
            ),
            outputs=3,
        ),
    )

    # Process sync channel
    g.add_chain(
        upstream=("route", "output-0"),
        nodes=(("sync_processed", tcspc.Delay(args.sync_delay)),),
    )
    # Process photon channel
    g.add_chain(
        upstream=("route", "output-1"),
        nodes=(
            tcspc.PairOneBetween(
                start_channel=args.photon_channels[0],
                stop_channels=(args.photon_channels[1],),
                time_window=args.max_photon_pulse_width,
                numeric_traits=numtraits,
            ),
            tcspc.Select(
                tcspc.DetectionPairEvent(numtraits),
                tcspc.TimeReachedEvent(numtraits),
            ),
            tcspc.TimeCorrelateAtMidpoint(numeric_traits=numtraits),
            tcspc.RemoveTimeCorrelation(numeric_traits=numtraits),
            (
                "photon_processed",
                tcspc.RecoverOrder(time_window=args.max_photon_pulse_width),
            ),
        ),
    )
    # Process pixel marker channel
    g.add_chain(
        upstream=("route", "output-2"),
        nodes=(
            tcspc.Match(
                tcspc.DetectionEvent(numtraits),
                pixel_start,
                matcher=tcspc.AlwaysMatcher(),
            ),
            tcspc.Select(pixel_start, tcspc.TimeReachedEvent(numtraits)),
            tcspc.Generate(
                trigger_event_type=pixel_start,
                output_event_type=pixel_stop,
                generator=tcspc.OneShotTimingGenerator(delay=args.pixel_time),
            ),
            tcspc.CheckAlternating(pixel_start, pixel_stop),
            (
                "pixels_processed",
                tcspc.StopWithError(
                    (tcspc.WarningEvent(),),
                    "Pixel time is such that pixel stop occurs after next pixel start.",
                ),
            ),
        ),
    )

    # Merge
    g.add_node(
        upstream={
            "input-0": "sync_processed",
            "input-1": "photon_processed",
        },
        name="merge-1",
        node=tcspc.Merge(
            tcspc.DetectionEvent(numtraits),
            tcspc.TimeReachedEvent(numtraits),
            max_buffered=merge_buffer_size,
        ),
    )
    g.add_chain(
        upstream="merge-1",
        nodes=(
            tcspc.PairAllBetween(
                start_channel=args.sync_channel,
                stop_channels=(args.photon_channels[1],),
                time_window=args.max_diff_time,
                numeric_traits=numtraits,
            ),
            tcspc.Select(
                tcspc.DetectionPairEvent(numtraits),
                tcspc.TimeReachedEvent(numtraits),
            ),
            (
                "merge1-processed",
                tcspc.TimeCorrelateAtStop(numeric_traits=numtraits),
            ),
        ),
    )
    g.add_node(
        upstream={
            "input-0": "merge1-processed",
            "input-1": "pixels_processed",
        },
        name="merge-2",
        node=tcspc.Merge(
            tcspc.TimeCorrelatedDetectionEvent(numtraits),
            pixel_start,
            pixel_stop,
            tcspc.TimeReachedEvent(numtraits),
            max_buffered=merge_buffer_size,
        ),
    )

    return tcspc.Subgraph(
        g,
        input_map={"input": g.inputs()[0]},
        output_map={"output": g.outputs()[0]},
    )


def _generate_histograms(
    settings: argparse.Namespace, reset: tcspc.CustomEvent
) -> tcspc.Subgraph:
    """Subgraph responsible for generating histograms from time-correlated detection events."""
    g = tcspc.Graph()
    # Convert time-correlated detection events into histogram bin increments
    nodes = [
        tcspc.MapToDatapoints(
            tcspc.TimeCorrelatedDetectionEvent(numtraits),
            tcspc.DifftimeDataMapper(numtraits),
            numtraits,
        ),
        tcspc.MapToBins(
            tcspc.LinearBinMapper(
                offset=0,
                bin_width=1,
                max_bin_index=255,
            )
        ),
        tcspc.ClusterBinIncrements(
            start_event_type=pixel_start,
            stop_event_type=pixel_stop,
        ),
        tcspc.Count(
            tcspc.BinIncrementClusterEvent(numtraits), PIXEL_COUNT_TAG
        ),
    ]

    if settings.sum:
        # Accumulate bin increments into histograms over all frames.
        nodes.extend(
            [
                tcspc.Append(reset.value()),
                tcspc.ScanHistograms(
                    num_elements=settings.width * settings.height,
                    num_bins=256,
                    max_per_bin=65535,
                    reset_event_type=reset,
                    emit_concluding=True,
                    numeric_traits=numtraits,
                ),
                tcspc.Count(
                    tcspc.HistogramArrayEvent(numtraits), FRAME_COUNT_TAG
                ),
                tcspc.Select(tcspc.ConcludingHistogramArrayEvent(numtraits)),
                tcspc.ExtractBucket(
                    tcspc.ConcludingHistogramArrayEvent(numtraits)
                ),
            ]
        )
    else:
        # Accumulate bin increments into per-frame histograms
        nodes.extend(
            [
                tcspc.ScanHistograms(
                    num_elements=settings.width * settings.height,
                    num_bins=256,
                    max_per_bin=65535,
                    clear_every_scan=True,
                    numeric_traits=numtraits,
                ),
                tcspc.Select(tcspc.HistogramArrayEvent(numtraits)),
                tcspc.Count(
                    tcspc.HistogramArrayEvent(numtraits), FRAME_COUNT_TAG
                ),
                tcspc.ExtractBucket(tcspc.HistogramArrayEvent(numtraits)),
            ]
        )

    g.add_chain(nodes=nodes)
    return tcspc.Subgraph(
        g,
        input_map={"input": g.inputs()[0]},
        output_map={"output": g.outputs()[0]},
    )


def _positive_int(s: str) -> int:
    v = int(s)
    if v <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return v


def _tuple(s: str) -> tuple[int, int]:
    try:
        a, b = s.split(",")
        return int(a), int(b)
    except Exception as e:
        raise argparse.ArgumentTypeError(
            "must be a tuple of two integers"
        ) from e


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument(
        "--sync-channel",
        type=int,
        required=True,
        help="Specify the channel containing the laser sync signal.",
    )
    p.add_argument(
        "--pixel-marker-channel",
        type=int,
        required=True,
        help="Specify the channel containing the pixel marker.",
    )
    p.add_argument(
        "--photon-channels",
        type=_tuple,
        required=True,
        help="Specify the channel containing the leading and trailing edges "
        "of photon pulses.",
    )
    p.add_argument(
        "--sync-delay",
        type=_positive_int,
        default=0,
        help="Specify how much to delay the the laser sync signal (in picoseconds) "
        "relative to the other signals. Negative values are allowed (and are "
        "typical).",
    )
    p.add_argument(
        "--max-photon-pulse-width",
        type=_positive_int,
        default=100000,
        help="Consider only photons with at most this much time between leading "
        "and trailing edges (in picoseconds).",
    )
    p.add_argument(
        "--max-diff-time",
        type=_positive_int,
        default=15000,
        help="Consider only photons within this much time since the previous laser sync.",
    )
    p.add_argument(
        "--pixel-time",
        type=_positive_int,
        required=True,
        help="Set pixel time (in picoseconds).",
    )
    p.add_argument(
        "--width",
        type=_positive_int,
        required=True,
        help="Set pixels per line.",
    )
    p.add_argument(
        "--height",
        type=_positive_int,
        required=True,
        help="Set lines per frame.",
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
    p.add_argument(
        "--dump-cpp-graph",
        action="store_true",
        help="do not process input; instead compile the graph, instantiate the "
        "processor, and emit the compiled C++ processor graph to standard "
        "output in Graphviz dot format. Requires a real input file (it is "
        "opened at processor construction time but never read); the output "
        "file is not opened",
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
    if (
        not args.dump_graph
        and not args.dump_cpp_graph
        and (
            args.pixel_time is None
            or args.input_file is None
            or args.output_file is None
        )
    ):
        print(
            "--pixel-time, input_file, and output_file are required",
            file=sys.stderr,
        )
        return 2

    if args.dump_graph:
        g = build_graph(args)
        print(g.to_graphviz())
        return 0

    if args.dump_cpp_graph:
        if (
            args.pixel_time is None
            or args.input_file is None
            or args.output_file is None
        ):
            print(
                "--pixel-time, input_file, and output_file are required",
                file=sys.stderr,
            )
            return 2
        g = build_graph(args)
        cg = tcspc.CompiledGraph(g)
        dump_ctx = tcspc.ExecutionContext(
            cg,
            {"filename": args.input_file, "pixel_time": args.pixel_time},
            (_BinFileSink(io.BytesIO()),),
        )
        print(dump_ctx.cpp_to_graphviz())
        return 0

    print("Creating processing graph...", file=sys.stderr)
    g = build_graph(args)

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
