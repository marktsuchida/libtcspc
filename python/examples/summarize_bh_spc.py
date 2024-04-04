# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import cppyy
import libtcspc as tcspc


def summarize(filename: str) -> int:
    g = tcspc.Graph()
    g.add_sequence(
        [
            tcspc.ReadBinaryStream(
                tcspc.BHSPCEvent,
                tcspc.VectorEvent(tcspc.BHSPCEvent),
                tcspc.BinaryFileInputStream(filename, start=4),
                -1,
                tcspc.ObjectPool(
                    tcspc.VectorEvent(tcspc.BHSPCEvent),
                    initial_count=0,
                    max_count=2,
                ),
                65536,
            ),
            tcspc.Stop((tcspc.WarningEvent,), "error reading input"),
            tcspc.DereferencePointer(
                tcspc.SharedPtrEvent(tcspc.VectorEvent(tcspc.BHSPCEvent))
            ),
            tcspc.Unbatch(
                tcspc.VectorEvent(tcspc.BHSPCEvent), tcspc.BHSPCEvent
            ),
            ("count-records", tcspc.Count(tcspc.BHSPCEvent)),
            tcspc.DecodeBHSPC(),
            tcspc.CheckMonotonic(),
            tcspc.Stop(
                (tcspc.WarningEvent, tcspc.DataLostEvent()),
                "error in data",
            ),
            ("count-phot", tcspc.Count(tcspc.TimeCorrelatedDetectionEvent())),
            ("count-mark", tcspc.Count(tcspc.MarkerEvent())),
            # Simplified for now compared to the C++ example (no per-channel
            # counts and time range).
        ]
    )
    g.add_node(None, tcspc.NullSink(), upstream="count-mark")

    ret = 0
    try:
        ctx = tcspc.ProcessorContext(g)
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
