# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import array
from typing import final

import pytest
from libtcspc._access import AccessTag
from libtcspc._compile import compile_graph
from libtcspc._cpp_utils import CppIdentifier, CppTypeName, uint8_type
from libtcspc._events import BucketEvent, EventType
from libtcspc._execute import PySink, create_execution_context
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import (
    Batch,
    Count,
    NullSink,
    SelectAll,
    SinkEvents,
    Stop,
)
from typing_extensions import override

IntEvent = EventType(CppTypeName("int"))


def test_execute_graph_with_single_input():
    g = Graph()
    g.add_node("a", NullSink())
    c = create_execution_context(compile_graph(g, (IntEvent,)), {})
    c.handle(123)
    c.flush()


def test_execute_node_access():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", NullSink(), upstream="c")
    c = create_execution_context(compile_graph(g, (IntEvent,)), {})
    c.handle(123)
    c.flush()
    assert c.access(AccessTag("counter")).count() == 1


def test_execute_rejects_events_and_flush_when_expired():
    g = Graph()
    g.add_node("a", NullSink())
    c = create_execution_context(compile_graph(g, (IntEvent,)), {})
    c.flush()
    with pytest.raises(RuntimeError):
        c.handle(123)
    with pytest.raises(RuntimeError):
        c.flush()


def test_execute_handles_buffer_events():
    g = Graph()
    g.add_node(
        "a", SinkEvents(EventType(CppTypeName("tcspc::bucket<tcspc::u8>")))
    )
    c = create_execution_context(
        compile_graph(g, [BucketEvent(uint8_type)]), {}
    )
    c.handle(b"")
    c.handle(b"abc")
    c.handle(memoryview(b""))
    c.handle(array.array("B", [1, 2, 3]))
    with pytest.raises(TypeError):
        c.handle(array.array("I", [1, 2, 3]))


def test_execute_require_parameter_with_no_default():
    g = Graph()
    g.add_node("a", Stop((), Param("a_msg")))
    g.add_node("s", NullSink(), upstream="a")
    cg = compile_graph(g)
    create_execution_context(cg, {CppIdentifier("a_msg"): "hello"})
    with pytest.raises(ValueError):
        create_execution_context(cg)
    with pytest.raises(ValueError):
        create_execution_context(cg, {})
    with pytest.raises(TypeError):
        create_execution_context(cg, {CppIdentifier("a_msg"): 123})


def test_execute_unknown_parameter():
    g = Graph()
    g.add_node("s", NullSink())
    cg = compile_graph(g)
    create_execution_context(cg)
    with pytest.raises(ValueError):
        create_execution_context(cg, {CppIdentifier("blah"): "hello"})


@final
class MockSink(PySink):
    def __init__(self, log) -> None:
        self._log = log

    @override
    def handle(self, event) -> None:
        self._log.append(f"handle({event})")

    @override
    def flush(self) -> None:
        self._log.append("flush()")


def test_execute_pass_through_integers():
    g = Graph()
    g.add_node("r", SelectAll())
    cg = compile_graph(g, (EventType(CppTypeName("int")),))

    log: list[str] = []
    sink = MockSink(log)
    c = create_execution_context(cg, None, (sink,))
    c.handle(42)
    assert log == ["handle(42)"]
    c.flush()
    assert log == ["handle(42)", "flush()"]


def test_execute_emit_bucket():
    g = Graph()
    g.add_node("b", Batch(EventType(CppTypeName("int")), batch_size=2))
    cg = compile_graph(g, (EventType(CppTypeName("int")),))

    log: list[str] = []
    sink = MockSink(log)
    c = create_execution_context(cg, None, (sink,))
    c.handle(42)
    assert len(log) == 0
    c.handle(43)
    assert log == ["handle([42 43])"]


def test_execute_sink_exception_propagates():
    g = Graph()
    g.add_node("r", SelectAll())
    cg = compile_graph(g, (EventType(CppTypeName("int")),))

    @final
    class RaisingSink(PySink):
        @override
        def handle(self, event) -> None:
            raise IndexError("test exception")

        @override
        def flush(self) -> None:
            pass

    sink = RaisingSink()
    c = create_execution_context(cg, None, (sink,))
    with pytest.raises(IndexError):
        c.handle(42)
