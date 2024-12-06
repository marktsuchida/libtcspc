# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import libtcspc as tcspc

RECORD_COUNT_TAG = tcspc.AccessTag("count_records")
PHOTON_COUNT_TAG = tcspc.AccessTag("count_phot")
MARK_COUNT_TAG = tcspc.AccessTag("count_mark")


def summarize(filename: str) -> int:
    print("Creating processing graph...")
    # BH SPC has no negative channel or marker numbers.
    dtypes = tcspc.DataTypes(channel_type=tcspc.CppTypeName("std::uint32_t"))

    g = tcspc.Graph()
    g.add_sequence(
        [
            tcspc.read_events_from_binary_file(
                tcspc.BHSPCEvent,
                tcspc.Param(tcspc.CppIdentifier("filename")),
                start_offset=4,
                stop_normally_on_error=True,
            ),
            tcspc.Count(tcspc.BHSPCEvent, RECORD_COUNT_TAG),
            tcspc.DecodeBHSPC(dtypes),
            tcspc.CheckMonotonic(dtypes),
            tcspc.Stop(
                (tcspc.WarningEvent, tcspc.DataLostEvent(dtypes)),
                "error in data",
            ),
            tcspc.Count(
                tcspc.TimeCorrelatedDetectionEvent(dtypes),
                PHOTON_COUNT_TAG,
            ),
            (
                "tail",
                tcspc.Count(tcspc.MarkerEvent(dtypes), MARK_COUNT_TAG),
            ),
            # Simplified for now compared to the C++ example (no per-channel
            # counts and time range).
        ]
    )
    g.add_node(None, tcspc.NullSink(), upstream="tail")

    ret = 0
    print("Compiling processing graph...", file=sys.stderr)
    cg = tcspc.compile_graph(g)
    ctx = tcspc.create_execution_context(
        cg, {tcspc.CppIdentifier("filename"): filename}
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

    print(f"Total record count = {ctx.access(RECORD_COUNT_TAG).count(): 9}")
    print(f"Photon count =       {ctx.access(PHOTON_COUNT_TAG).count(): 9}")
    print(f"Marker count =       {ctx.access(MARK_COUNT_TAG).count(): 9}")
    return ret


def main() -> int:
    if len(sys.argv) != 2:
        print("A single argument (the filename) is required", file=sys.stderr)
        return 2
    return summarize(sys.argv[1])


if __name__ == "__main__":
    sys.exit(main())
