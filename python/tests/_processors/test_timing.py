# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _uint64_type,
)
from libtcspc._param import Param
from libtcspc._processors import CountDownTo, CountUpTo

TickEvent = _NamedEvent(_CppTypeName("int"))
FireEvent = _NamedEvent(_CppTypeName("long"))
ResetEvent = _NamedEvent(_CppTypeName("short"))
OtherEvent = _NamedEvent(_CppTypeName("unsigned"))

TriggerEvent = _NamedEvent(_CppTypeName("int"))
OutputEvent = _NamedEvent(_CppTypeName("long"))

InEvent = _NamedEvent(_CppTypeName("int"))
OutEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_CountDownTo_output_includes_fire_event():
    node = CountDownTo(TickEvent, FireEvent, ResetEvent, 5, 0, 10)
    out = node._map_event_sets([(TickEvent, OtherEvent)])
    assert FireEvent in out[0]


def test_CountDownTo_codegen_calls_tcspc_count_down_to():
    node = CountDownTo(TickEvent, FireEvent, ResetEvent, 5, 0, 10)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::count_down_to<" in code
    assert "false" in code
    assert "DOWN" in code


def test_CountDownTo_params_wire():
    node = CountDownTo(
        TickEvent,
        FireEvent,
        ResetEvent,
        Param("t"),
        Param("l"),
        Param("i"),
    )
    params = node._parameters()
    assert (Param("t"), _uint64_type) in params
    assert (Param("l"), _uint64_type) in params
    assert (Param("i"), _uint64_type) in params


def test_CountUpTo_output_includes_fire_event():
    node = CountUpTo(TickEvent, FireEvent, ResetEvent, 10, 100, 0)
    out = node._map_event_sets([(TickEvent, OtherEvent)])
    assert len(out) == 1
    assert TickEvent in out[0]
    assert OtherEvent in out[0]
    assert FireEvent in out[0]


def test_CountUpTo_output_does_not_duplicate_fire_event():
    node = CountUpTo(TickEvent, FireEvent, ResetEvent, 10, 100, 0)
    out = node._map_event_sets([(TickEvent, FireEvent, OtherEvent)])
    assert len(out[0]) == 3


def test_CountUpTo_codegen_calls_tcspc_count_up_to():
    node = CountUpTo(TickEvent, FireEvent, ResetEvent, 10, 100, 0)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::count_up_to<" in code
    assert "int" in code
    assert "long" in code
    assert "short" in code
    assert "false" in code  # default fire_after_tick
    assert "DOWN" in code


def test_CountUpTo_codegen_includes_threshold_limit_initial():
    node = CountUpTo(TickEvent, FireEvent, ResetEvent, 10, 100, 5)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::arg::threshold<tcspc::u64>{tcspc::u64{10uLL}}" in code
    assert "tcspc::arg::limit<tcspc::u64>{tcspc::u64{100uLL}}" in code
    assert "tcspc::arg::initial_count<tcspc::u64>{tcspc::u64{5uLL}}" in code


def test_CountUpTo_fire_after_tick_true():
    node = CountUpTo(
        TickEvent, FireEvent, ResetEvent, 10, 100, 0, fire_after_tick=True
    )
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "true" in code


def test_CountUpTo_params_wire():
    node = CountUpTo(
        TickEvent,
        FireEvent,
        ResetEvent,
        Param("t"),
        Param("l"),
        Param("i"),
    )
    params = node._parameters()
    assert (Param("t"), _uint64_type) in params
    assert (Param("l"), _uint64_type) in params
    assert (Param("i"), _uint64_type) in params


def test_CountUpTo_compiles_end_to_end():
    from libtcspc._compile import CompiledGraph
    from libtcspc._execute import ExecutionContext
    from libtcspc._graph import Graph

    g = Graph()
    g.add_node("dec", tcspc.DecodeBHSPC())
    g.add_node(
        "counter",
        CountUpTo(
            tcspc.TimeCorrelatedDetectionEvent(),
            tcspc.TimeReachedEvent(),
            tcspc.WarningEvent(),
            10,
            100,
            0,
        ),
        upstream="dec",
    )
    g.add_node(None, tcspc.SinkAll(), upstream="counter")
    cg = CompiledGraph(g, (tcspc.BHSPCEvent(),))
    ctx = ExecutionContext(cg)
    ctx.flush()


def test_Generate_event_set_adds_output():
    node = tcspc.Generate(
        TriggerEvent, OutputEvent, tcspc.NullTimingGenerator()
    )
    (out,) = node._map_event_sets([(TriggerEvent,)])
    cpp = [e._cpp_type_name() for e in out]
    assert TriggerEvent._cpp_type_name() in cpp
    assert OutputEvent._cpp_type_name() in cpp


def test_Generate_codegen():
    node = tcspc.Generate(
        TriggerEvent, OutputEvent, tcspc.OneShotTimingGenerator(50)
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::generate<\n                int,\n                long\n"
        in code
    )
    assert "tcspc::one_shot_timing_generator<" in code
    assert "DOWN" in code


def test_Match_event_set_adds_out_event():
    node = tcspc.Match(InEvent, OutEvent, tcspc.AlwaysMatcher())
    (out,) = node._map_event_sets([(InEvent,)])
    cpp = [e._cpp_type_name() for e in out]
    assert InEvent._cpp_type_name() in cpp
    assert OutEvent._cpp_type_name() in cpp


def test_Match_codegen():
    node = tcspc.Match(InEvent, OutEvent, tcspc.ChannelMatcher(3))
    code = node._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::match<\n                int,\n                long\n" in code
    )
    assert "tcspc::channel_matcher<" in code
    assert "DOWN" in code


def test_Match_forwards_matcher_params():
    node = tcspc.Match(InEvent, OutEvent, tcspc.ChannelMatcher(Param("ch")))
    assert len(node._parameters()) == 1


def test_MatchAndConsume_event_set_adds_out_event():
    node = tcspc.MatchAndConsume(InEvent, OutEvent, tcspc.AlwaysMatcher())
    (out,) = node._map_event_sets([(InEvent,)])
    cpp = [e._cpp_type_name() for e in out]
    assert OutEvent._cpp_type_name() in cpp


def test_MatchAndConsume_codegen():
    node = tcspc.MatchAndConsume(InEvent, OutEvent, tcspc.AlwaysMatcher())
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::match_and_consume<" in code
    assert "tcspc::always_matcher()" in code
    assert "DOWN" in code
