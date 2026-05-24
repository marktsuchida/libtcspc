# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier
from libtcspc._param import Param

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
    from libtcspc._compile import CompiledGraph
    from libtcspc._execute import ExecutionContext
    from libtcspc._graph import Graph

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
