# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import cppyy
import libtcspc as tcspc

cppyy.include("cstdint")


def summarize(filename: str) -> int:
    # BH SPC has no negative channel or marker numbers.
    dtraits = tcspc.DataTraits(channel_type="std::uint32_t")

    g = tcspc.Graph()
    g.add_sequence(
        [
            tcspc.read_events_from_binary_file(
                tcspc.BHSPCEvent,
                filename,
                start_offset=4,
                stop_normally_on_error=True,
            ),
            ("count-records", tcspc.Count(tcspc.BHSPCEvent)),
            tcspc.DecodeBHSPC(dtraits),
            tcspc.CheckMonotonic(dtraits),
            tcspc.Stop(
                (tcspc.WarningEvent, tcspc.DataLostEvent(dtraits)),
                "error in data",
            ),
            (
                "count-phot",
                tcspc.Count(tcspc.TimeCorrelatedDetectionEvent(dtraits)),
            ),
            ("count-mark", tcspc.Count(tcspc.MarkerEvent(dtraits))),
            # Simplified for now compared to the C++ example (no per-channel
            # counts and time range).
        ]
    )
    g.add_node(None, tcspc.NullSink(), upstream="count-mark")

    ret = 0
    try:
        ctx = tcspc.Context(g)
        ctx.flush()
    except tcspc.EndProcessing as e:
        print(f"Stopped because: {e}")
        print("The following results are up to where processing stopped.")
        ret = 1
    except cppyy.gbl.std.exception as e:
        print(e.what())
        return 2

    print(f"Total record count = {ctx.access("count-records").count(): 9}")
    print(f"Photon count =       {ctx.access("count-phot").count(): 9}")
    print(f"Marker count =       {ctx.access("count-mark").count(): 9}")
    return ret


def main():
    if len(sys.argv) != 2:
        print("A single argument (the filename) is required", file=sys.stderr)
        return 1
    return summarize(sys.argv[1])


if __name__ == "__main__":
    sys.exit(main())
