# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import numpy as np
import pytest
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier
from libtcspc._execute import ExecutionContext
from libtcspc._graph import Graph
from libtcspc._param import Param
from typing_extensions import override

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_Histogram_event_set():
    node = tcspc.Histogram(16, 255)
    (out,) = node._map_event_sets([(tcspc.BinIncrementEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.HistogramEvent()._cpp_type_name() in cpp
    assert tcspc.BinIncrementEvent()._cpp_type_name() not in cpp


def test_Histogram_emit_concluding_adds_event():
    node = tcspc.Histogram(16, 255, emit_concluding=True)
    (out,) = node._map_event_sets([(tcspc.BinIncrementEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.ConcludingHistogramEvent()._cpp_type_name() in cpp


def test_Histogram_saturate_adds_warning():
    node = tcspc.Histogram(16, 255, overflow="saturate")
    (out,) = node._map_event_sets([(tcspc.BinIncrementEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.WarningEvent()._cpp_type_name() in cpp


def test_Histogram_codegen():
    node = tcspc.Histogram(16, 255, tcspc.MarkerEvent(), emit_concluding=True)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::histogram<(" in code
    assert "emit_concluding_events" in code
    assert "tcspc::marker_event<" in code
    assert "tcspc::arg::num_bins<std::size_t>" in code
    assert "tcspc::arg::max_per_bin<" in code
    assert "DOWN" in code


def test_Histogram_default_reset_is_never_event():
    code = tcspc.Histogram(16, 255)._cpp_expression(gencontext, DOWN)
    assert "tcspc::never_event" in code


def test_Histogram_rejects_bad_overflow():
    with pytest.raises(ValueError):
        tcspc.Histogram(16, 255, overflow="bogus")


def test_Histogram_params():
    node = tcspc.Histogram(Param("nb"), Param("mpb"))
    assert len(node._parameters()) == 2


def test_Histogram_pipeline_end_to_end():
    g = Graph()
    g.add_node(
        "md",
        tcspc.MapToDatapoints(
            tcspc.TimeCorrelatedDetectionEvent(), tcspc.DifftimeDataMapper()
        ),
    )
    g.add_node(
        "mb", tcspc.MapToBins(tcspc.LinearBinMapper(0, 1, 100)), upstream="md"
    )
    g.add_node("hist", tcspc.Histogram(101, 65535), upstream="mb")
    g.add_node(
        "ext", tcspc.ExtractBucket(tcspc.HistogramEvent()), upstream="hist"
    )
    g.add_node(None, tcspc.SinkAll(), upstream="ext")
    cg = CompiledGraph(g, (tcspc.TimeCorrelatedDetectionEvent(),))
    ctx = ExecutionContext(cg)
    ctx.flush()


def test_ScanHistograms_event_set():
    node = tcspc.ScanHistograms(4, 16, 255)
    (out,) = node._map_event_sets([(tcspc.BinIncrementClusterEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.HistogramArrayProgressEvent()._cpp_type_name() in cpp
    assert tcspc.HistogramArrayEvent()._cpp_type_name() in cpp
    assert tcspc.BinIncrementClusterEvent()._cpp_type_name() not in cpp


def test_ScanHistograms_emit_concluding():
    node = tcspc.ScanHistograms(4, 16, 255, emit_concluding=True)
    (out,) = node._map_event_sets([(tcspc.BinIncrementClusterEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.ConcludingHistogramArrayEvent()._cpp_type_name() in cpp


def test_ScanHistograms_codegen():
    node = tcspc.ScanHistograms(
        4, 16, 255, reset_after_scan=True, clear_every_scan=True
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::scan_histograms<(" in code
    assert "reset_after_scan" in code
    assert "clear_every_scan" in code
    assert "tcspc::arg::num_elements<std::size_t>" in code
    assert "tcspc::arg::num_bins<std::size_t>" in code
    assert "tcspc::arg::max_per_bin<" in code
    assert "DOWN" in code


def test_ScanHistograms_rejects_bad_overflow():
    with pytest.raises(ValueError):
        tcspc.ScanHistograms(4, 16, 255, overflow="bogus")


class _Uint16BucketSource(tcspc.PyBucketSource):
    def __init__(self) -> None:
        self.buffers: list[np.ndarray] = []

    @override
    def bucket_of_size(self, size: int):
        b = np.zeros(size, dtype=np.uint16)
        self.buffers.append(b)
        return b


class _RecordingSink(tcspc.PySink):
    def __init__(self) -> None:
        self.received: list = []

    @override
    def handle(self, event) -> None:
        self.received.append(event)

    @override
    def flush(self) -> None:
        pass


def _received_of_type(sink: _RecordingSink, event_type: tcspc.EventType):
    return [
        e
        for e in sink.received
        if isinstance(e, tcspc.EventInstance) and e._event_type == event_type
    ]


def test_Histogram_delivers_HistogramEvent_to_python_sink():
    g = Graph()
    g.add_node("hist", tcspc.Histogram(4, 65535))
    cg = CompiledGraph(g, (tcspc.BinIncrementEvent(),))
    sink = _RecordingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(tcspc.BinIncrementEvent().value(bin_index=1))
    ec.handle(tcspc.BinIncrementEvent().value(bin_index=1))
    ec.flush()
    assert len(sink.received) == 2
    first, second = sink.received
    arr1 = first.data_bucket
    arr2 = second.data_bucket
    assert isinstance(arr1, np.ndarray)
    assert isinstance(arr2, np.ndarray)
    assert arr1.dtype == np.dtype(np.uint16)
    assert arr1.flags.writeable is False
    # Each delivered snapshot is independent of later accumulation.
    assert list(arr1) == [0, 1, 0, 0]
    assert list(arr2) == [0, 2, 0, 0]


def test_Histogram_concluding_event_zero_copy_with_PyBucketSource():
    reset = tcspc.CustomEvent("ce_hist_zero_copy_reset")
    source = _Uint16BucketSource()
    g = Graph()
    g.add_node(
        "hist",
        tcspc.Histogram(
            4,
            65535,
            reset,
            buffer_provider=Param("bufsrc"),
            emit_concluding=True,
        ),
    )
    cg = CompiledGraph(g, (tcspc.BinIncrementEvent(), reset))
    sink = _RecordingSink()
    ec = ExecutionContext(cg, {"bufsrc": source}, (sink,))
    ec.handle(tcspc.BinIncrementEvent().value(bin_index=2))
    ec.handle(reset.value())
    concluding = _received_of_type(sink, tcspc.ConcludingHistogramEvent())
    assert len(concluding) == 1
    arr = concluding[0].data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [0, 0, 1, 0]
    assert arr.flags.writeable is False
    # Zero-copy: the delivered array aliases a source-provided buffer.
    assert any(np.shares_memory(arr, b) for b in source.buffers)


def test_Histogram_concluding_event_shared_through_Broadcast():
    reset = tcspc.CustomEvent("ce_hist_broadcast_reset")
    source = _Uint16BucketSource()
    g = Graph()
    g.add_node(
        "hist",
        tcspc.Histogram(
            4,
            65535,
            reset,
            buffer_provider=Param("bufsrc"),
            emit_concluding=True,
        ),
    )
    g.add_node(
        "bc",
        tcspc.Broadcast(
            reset,
            tcspc.HistogramEvent(),
            tcspc.ConcludingHistogramEvent(),
            outputs=2,
        ),
        upstream="hist",
    )
    cg = CompiledGraph(g, (tcspc.BinIncrementEvent(), reset))
    sink0, sink1 = _RecordingSink(), _RecordingSink()
    ec = ExecutionContext(cg, {"bufsrc": source}, (sink0, sink1))
    ec.handle(tcspc.BinIncrementEvent().value(bin_index=3))
    ec.handle(reset.value())
    for sink in (sink0, sink1):
        concluding = _received_of_type(sink, tcspc.ConcludingHistogramEvent())
        assert len(concluding) == 1
        arr = concluding[0].data_bucket
        assert isinstance(arr, np.ndarray)
        assert list(arr) == [0, 0, 0, 1]
        assert any(np.shares_memory(arr, b) for b in source.buffers)


def test_RecordLast_records_bucket_carrying_event():
    tag = tcspc.AccessTag("last-hist")
    g = Graph()
    g.add_node("hist", tcspc.Histogram(4, 65535))
    g.add_node(
        "rec", tcspc.RecordLast(tcspc.HistogramEvent(), tag), upstream="hist"
    )
    cg = CompiledGraph(g, (tcspc.BinIncrementEvent(),))
    sink = _RecordingSink()
    ec = ExecutionContext(cg, None, (sink,))
    acc = ec.access(tag)
    assert acc.get() is None
    ec.handle(tcspc.BinIncrementEvent().value(bin_index=0))
    last = acc.get()
    assert isinstance(last, tcspc.EventInstance)
    arr = last.data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [1, 0, 0, 0]
