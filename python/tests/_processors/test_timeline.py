# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _int64_type,
    _size_type,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._param import Param
from libtcspc._processors import Delay, RebaseAbstime, RegulateTimeReached

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Delay_event_set_is_preserved():
    node = Delay(10)
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_Delay_codegen_int_delta():
    node = Delay(42)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::delay<" in code
    assert NumericTraits()._cpp_type_name() in code
    assert "tcspc::i64{42LL}" in code
    assert "DOWN" in code


def test_Delay_codegen_negative_delta():
    node = Delay(-5)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::i64{-5LL}" in code


def test_Delay_param_wires_int64():
    node = Delay(Param("d"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("d"), _int64_type)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "params.z_d" in code


def test_Delay_compiles_end_to_end():
    import libtcspc as tcspc
    from libtcspc._compile import CompiledGraph
    from libtcspc._execute import ExecutionContext
    from libtcspc._graph import Graph

    g = Graph()
    g.add_node("dec", tcspc.DecodeBHSPC())
    g.add_node(
        "filter", tcspc.SelectExcept(tcspc.WarningEvent()), upstream="dec"
    )
    g.add_node("delay", Delay(100), upstream="filter")
    g.add_node(None, tcspc.SinkAll(), upstream="delay")
    cg = CompiledGraph(g, (tcspc.BHSPCEvent(),))
    ctx = ExecutionContext(cg)
    ctx.flush()


def test_RebaseAbstime_event_set_is_preserved():
    node = RebaseAbstime()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_RebaseAbstime_codegen():
    node = RebaseAbstime()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::rebase_abstime<" in code
    assert NumericTraits()._cpp_type_name() in code
    assert "DOWN" in code


def test_RegulateTimeReached_event_set_is_preserved():
    node = RegulateTimeReached(100, 1000)
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_RegulateTimeReached_codegen():
    node = RegulateTimeReached(100, 1000)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::regulate_time_reached<" in code
    assert "tcspc::i64{100LL}" in code
    assert "std::size_t{1000uLL}" in code
    assert "DOWN" in code


def test_RegulateTimeReached_params_wire():
    node = RegulateTimeReached(Param("it"), Param("ct"))
    params = node._parameters()
    assert (Param("it"), _int64_type) in params
    assert (Param("ct"), _size_type) in params
