# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import array

import pytest
from libtcspc._access import AccessTag
from libtcspc._compile import compile_graph
from libtcspc._cpp_utils import CppIdentifier, CppTypeName
from libtcspc._events import BufferSpanEvent, EventType
from libtcspc._execute import create_execution_context
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import Count, NullSink, SinkEvents, Stop

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
        "a", SinkEvents(EventType(CppTypeName("tcspc::span<tcspc::u8 const>")))
    )
    c = create_execution_context(
        compile_graph(g, [BufferSpanEvent(CppTypeName("tcspc::u8"))]),
        {},
    )
    c.handle(b"")
    c.handle(b"abc")
    c.handle(memoryview(b""))
    c.handle(array.array("B", [1, 2, 3]))
    with pytest.raises(TypeError):
        c.handle(array.array("I", [1, 2, 3]))


def test_execute_require_parameter_with_no_default():
    g = Graph()
    g.add_node("a", Stop((), Param(CppIdentifier("a_msg"))))
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
    with pytest.raises(KeyError):
        create_execution_context(cg, {CppIdentifier("blah"): "hello"})
