# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._access import AccessTag
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._execute import ExecutionContext
from libtcspc._graph import Graph
from libtcspc._processors import Append, Count, Prepend, SinkAll, SourceNothing

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_SinkAll_has_no_output_ports():
    node = SinkAll()
    assert node.outputs() == ()


def test_SinkAll_accepts_any_input_event_set():
    node = SinkAll()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == ()
    assert node._map_event_sets([()]) == ()


def test_SinkAll_codegen_is_tcspc_sink_all():
    node = SinkAll()
    assert node._cpp_expression(gencontext, []) == "tcspc::sink_all()"


def test_SourceNothing_rejects_nonempty_input_set():
    node = SourceNothing()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_SourceNothing_output_event_set_is_empty():
    node = SourceNothing()
    assert node._map_event_sets([()]) == ((),)


def test_SourceNothing_codegen_calls_tcspc_source_nothing():
    node = SourceNothing()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::source_nothing(" in code
    assert "DOWN" in code


def test_Prepend_event_set_adds_inserted_event():
    node = Prepend(tcspc.DetectionEvent().value(abstime=0, channel=0))
    out = node._relay_map_event_set((IntEvent,))
    cpp = [e._cpp_type_name() for e in out]
    assert IntEvent._cpp_type_name() in cpp
    assert tcspc.DetectionEvent()._cpp_type_name() in cpp


def test_Prepend_event_set_does_not_duplicate_inserted_event():
    det = tcspc.DetectionEvent()
    node = Prepend(det.value(abstime=0, channel=0))
    out = node._relay_map_event_set((det,))
    assert (
        sum(1 for e in out if e._cpp_type_name() == det._cpp_type_name()) == 1
    )


def test_Prepend_codegen():
    node = Prepend(tcspc.DetectionEvent().value(abstime=5, channel=3))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::prepend(")
    assert ".abstime = static_cast<" in code
    assert "(5)" in code
    assert ".channel = static_cast<" in code
    assert "(3)" in code
    assert "DOWN" in code


def test_Append_event_set_adds_inserted_event():
    node = Append(tcspc.DetectionEvent().value(abstime=0, channel=0))
    out = node._relay_map_event_set((IntEvent,))
    cpp = [e._cpp_type_name() for e in out]
    assert IntEvent._cpp_type_name() in cpp
    assert tcspc.DetectionEvent()._cpp_type_name() in cpp


def test_Append_event_set_from_empty_input():
    node = Append(tcspc.DetectionEvent().value(abstime=0, channel=0))
    out = node._relay_map_event_set(())
    cpp = [e._cpp_type_name() for e in out]
    assert cpp == [tcspc.DetectionEvent()._cpp_type_name()]


def test_Append_codegen():
    node = Append(tcspc.DetectionEvent().value(abstime=5, channel=3))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::append(")
    assert ".abstime = static_cast<" in code
    assert "DOWN" in code


def test_Prepend_compiles_and_fires_once_end_to_end():
    g = Graph()
    g.add_node(
        "prep",
        Prepend(tcspc.DetectionEvent().value(abstime=42, channel=1)),
    )
    g.add_node(
        "count",
        Count(tcspc.DetectionEvent(), AccessTag("c")),
        upstream="prep",
    )
    g.add_node(None, SinkAll(), upstream="count")
    ctx = ExecutionContext(CompiledGraph(g, (IntEvent,)))
    assert ctx.access(AccessTag("c")).count() == 0
    ctx.handle(10)
    assert ctx.access(AccessTag("c")).count() == 1
    ctx.handle(20)
    assert ctx.access(AccessTag("c")).count() == 1
    ctx.flush()


def test_Append_compiles_and_fires_at_flush_end_to_end():
    g = Graph()
    g.add_node(
        "app",
        Append(tcspc.DetectionEvent().value(abstime=42, channel=1)),
    )
    g.add_node(
        "count",
        Count(tcspc.DetectionEvent(), AccessTag("c")),
        upstream="app",
    )
    g.add_node(None, SinkAll(), upstream="count")
    ctx = ExecutionContext(CompiledGraph(g, ()))
    assert ctx.access(AccessTag("c")).count() == 0
    ctx.flush()
    assert ctx.access(AccessTag("c")).count() == 1
