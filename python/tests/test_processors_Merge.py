# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._execute import ExecutionContext
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import Merge

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Merge_has_two_input_ports():
    node = Merge(IntEvent)
    assert node.inputs() == ("input-0", "input-1")


def test_Merge_has_single_output_port():
    node = Merge(IntEvent)
    assert node.outputs() == ("output",)


def test_Merge_map_event_sets_returns_declared_types():
    node = Merge(IntEvent, OtherEvent)
    result = node._map_event_sets([(IntEvent,), (OtherEvent,)])
    assert result == ((IntEvent, OtherEvent),)


def test_Merge_map_event_sets_rejects_wrong_number_of_inputs():
    node = Merge(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,), (IntEvent,), (IntEvent,)])


def test_Merge_map_event_sets_rejects_unconfigured_event():
    node = Merge(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,), (OtherEvent,)])


def test_Merge_parameters_empty_for_int_max_buffered():
    node = Merge(IntEvent, max_buffered=1024)
    assert len(node._parameters()) == 0


def test_Merge_parameters_forward_param_max_buffered():
    p: Param = Param("mb")
    node = Merge(IntEvent, max_buffered=p)
    params = node._parameters()
    assert len(params) == 1
    assert params[0][0] is p


def test_Merge_codegen_calls_tcspc_merge():
    node = Merge(IntEvent, OtherEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("d0")])
    assert (
        "tcspc::merge<tcspc::type_list<int, long>, "
        "tcspc::default_numeric_traits>(" in code
    )
    assert (
        "tcspc::arg::max_buffered<std::size_t>{std::size_t{65536uLL}}" in code
    )
    assert "d0)" in code


def test_Merge_codegen_param_max_buffered_references_params():
    node = Merge(IntEvent, max_buffered=Param("mb"))
    code = node._cpp_expression(gencontext, [_CppExpression("d0")])
    assert "tcspc::arg::max_buffered<std::size_t>{params.z_mb}" in code


def test_Merge_codegen_rejects_wrong_number_of_downstreams():
    node = Merge(IntEvent)
    with pytest.raises(ValueError):
        node._cpp_expression(
            gencontext, [_CppExpression("d0"), _CppExpression("d1")]
        )


def _merge_diamond_graph():
    import libtcspc as tcspc
    from libtcspc._routers import ChannelRouter

    dt = tcspc.NumericTraits()
    g = Graph()
    g.add_node("dec", tcspc.DecodeBHSPC(dt))
    g.add_node(
        "sel",
        tcspc.Select(tcspc.TimeCorrelatedDetectionEvent(dt)),
        upstream="dec",
    )
    g.add_node(
        "rt",
        tcspc.Route(
            tcspc.TimeCorrelatedDetectionEvent(dt),
            router=ChannelRouter({0: 0, 1: 1}),
            outputs=2,
        ),
        upstream="sel",
    )
    g.add_node("mrg", Merge(tcspc.TimeCorrelatedDetectionEvent(dt)))
    g.add_node("snk", tcspc.SinkAll(), upstream="mrg")
    g.connect(("rt", "output-0"), ("mrg", "input-0"))
    g.connect(("rt", "output-1"), ("mrg", "input-1"))
    return g


def test_Merge_diamond_compiles_and_runs_end_to_end():
    import libtcspc as tcspc

    g = _merge_diamond_graph()
    cg = CompiledGraph(g, (tcspc.BHSPCEvent(),))
    ExecutionContext(cg).flush()
