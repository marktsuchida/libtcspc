# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import final

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._graph import Graph
from libtcspc._processors import Broadcast
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Broadcast_port_naming_int_form():
    node = Broadcast(IntEvent, outputs=3)
    assert node.outputs() == ("output-0", "output-1", "output-2")


def test_Broadcast_port_naming_sequence_form():
    node = Broadcast(IntEvent, outputs=("a", "b"))
    assert node.outputs() == ("a", "b")


def test_Broadcast_has_single_input_port():
    node = Broadcast(IntEvent, outputs=2)
    assert node.inputs() == ("input",)


def test_Broadcast_rejects_zero_outputs():
    with pytest.raises(ValueError):
        Broadcast(IntEvent, outputs=0)
    with pytest.raises(ValueError):
        Broadcast(IntEvent, outputs=())


def test_Broadcast_rejects_duplicate_output_names():
    with pytest.raises(ValueError):
        Broadcast(IntEvent, outputs=("a", "a"))


def test_Broadcast_map_event_sets_returns_event_types_per_output():
    node = Broadcast(IntEvent, OtherEvent, outputs=2)
    result = node._map_event_sets([(IntEvent,)])
    assert result == ((IntEvent, OtherEvent), (IntEvent, OtherEvent))


def test_Broadcast_map_event_sets_rejects_wrong_number_of_inputs():
    node = Broadcast(IntEvent, outputs=2)
    with pytest.raises(ValueError):
        node._map_event_sets([])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,), (IntEvent,)])


def test_Broadcast_map_event_sets_rejects_unconfigured_event():
    node = Broadcast(IntEvent, outputs=2)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, OtherEvent)])


def test_Broadcast_codegen_calls_tcspc_broadcast():
    node = Broadcast(IntEvent, OtherEvent, outputs=2)
    code = node._cpp_expression(
        gencontext, [_CppExpression("d0"), _CppExpression("d1")]
    )
    assert "tcspc::broadcast<tcspc::type_list<int, long>>(d0, d1)" in code


def test_Broadcast_codegen_rejects_wrong_number_of_downstreams():
    node = Broadcast(IntEvent, outputs=2)
    with pytest.raises(ValueError):
        node._cpp_expression(gencontext, [_CppExpression("d0")])


@final
class MockSink(PySink):
    def __init__(self, log: list[str]) -> None:
        self._log = log

    @override
    def handle(self, event) -> None:
        self._log.append(f"handle({event})")

    @override
    def flush(self) -> None:
        self._log.append("flush()")


def test_Broadcast_end_to_end_delivers_to_all_outputs():
    int_event = _NamedEvent(_CppTypeName("int"))
    g = Graph()
    g.add_node("bc", Broadcast(int_event, outputs=2))
    # Both of bc's output ports are unconnected, so they become the graph's
    # two output ports -> two sinks.
    cg = CompiledGraph(g, (int_event,))

    log0: list[str] = []
    log1: list[str] = []
    c = ExecutionContext(cg, None, (MockSink(log0), MockSink(log1)))
    c.handle(42)
    assert log0 == ["handle(42)"]
    assert log1 == ["handle(42)"]
    c.flush()
    assert log0 == ["handle(42)", "flush()"]
    assert log1 == ["handle(42)", "flush()"]
