# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._access import AccessTag, _UniqueBinMapperAccessorSpec
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

StartEvent = _NamedEvent(_CppTypeName("long"))
StopEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_ClusterBinIncrements_event_set():
    node = tcspc.ClusterBinIncrements(StartEvent, StopEvent)
    (out,) = node._map_event_sets(
        [(tcspc.BinIncrementEvent(), StartEvent, StopEvent)]
    )
    assert out == (tcspc.BinIncrementClusterEvent(),)


def test_ClusterBinIncrements_passes_others():
    other = _NamedEvent(_CppTypeName("char"))
    node = tcspc.ClusterBinIncrements(StartEvent, StopEvent)
    (out,) = node._map_event_sets(
        [(tcspc.BinIncrementEvent(), StartEvent, StopEvent, other)]
    )
    cpp = [e._cpp_type_name() for e in out]
    assert other._cpp_type_name() in cpp
    assert tcspc.BinIncrementClusterEvent()._cpp_type_name() in cpp


def test_ClusterBinIncrements_codegen():
    node = tcspc.ClusterBinIncrements(StartEvent, StopEvent)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::cluster_bin_increments<" in code
    assert "long" in code and "short" in code
    assert "DOWN" in code


def test_MapToBins_event_set():
    node = tcspc.MapToBins(tcspc.LinearBinMapper(0, 1, 100))
    (out,) = node._map_event_sets([(tcspc.DatapointEvent(),)])
    assert out == (tcspc.BinIncrementEvent(),)


def test_MapToBins_codegen():
    node = tcspc.MapToBins(tcspc.LinearBinMapper(0, 1, 100))
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::map_to_bins<tcspc::default_numeric_traits>(" in code
    assert "tcspc::linear_bin_mapper<" in code
    assert "DOWN" in code


def test_MapToBins_forwards_unique_accesses():
    tag = AccessTag("u")
    node = tcspc.MapToBins(tcspc.UniqueBinMapper(tag, 100))
    accesses = node._accesses()
    assert len(accesses) == 1
    ((got_tag, spec),) = accesses
    assert got_tag == tag
    assert isinstance(spec, _UniqueBinMapperAccessorSpec)


def test_MapToBins_no_accesses_for_plain_mapper():
    node = tcspc.MapToBins(tcspc.LinearBinMapper(0, 1, 100))
    assert node._accesses() == ()


def test_MapToBins_unique_access_end_to_end():
    from libtcspc._compile import CompiledGraph
    from libtcspc._execute import ExecutionContext
    from libtcspc._graph import Graph

    tag = AccessTag("ubm")
    g = Graph()
    g.add_node("mb", tcspc.MapToBins(tcspc.UniqueBinMapper(tag, 100)))
    g.add_node(None, tcspc.SinkAll(), upstream="mb")
    cg = CompiledGraph(g, (tcspc.DatapointEvent(),))
    ctx = ExecutionContext(cg)
    ctx.flush()
    assert ctx.access(tag).values() == []


def test_MapToDatapoints_event_set():
    node = tcspc.MapToDatapoints(
        tcspc.TimeCorrelatedDetectionEvent(), tcspc.DifftimeDataMapper()
    )
    (out,) = node._map_event_sets([(tcspc.TimeCorrelatedDetectionEvent(),)])
    assert out == (tcspc.DatapointEvent(),)


def test_MapToDatapoints_codegen():
    node = tcspc.MapToDatapoints(
        tcspc.TimeCorrelatedDetectionEvent(), tcspc.DifftimeDataMapper()
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::map_to_datapoints<" in code
    assert "tcspc::time_correlated_detection_event<" in code
    assert "tcspc::difftime_data_mapper<" in code
    assert "DOWN" in code
