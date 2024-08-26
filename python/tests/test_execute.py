# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import array

import cppyy
import pytest
from libtcspc._access import AccessTag
from libtcspc._compile import compile_graph
from libtcspc._events import EventType
from libtcspc._execute import create_execution_context
from libtcspc._graph import Graph
from libtcspc._processors import Count, NullSink, SinkEvents

cppyy.include("string")


def test_execute_graph_with_single_input():
    g = Graph()
    g.add_node("a", NullSink())
    c = create_execution_context(compile_graph(g, (EventType("int"),)))
    c.handle(123)
    c.flush()


def test_execute_node_access():
    g = Graph()
    g.add_node("c", Count(EventType("int"), AccessTag("counter")))
    g.add_node("a", NullSink(), upstream="c")
    c = create_execution_context(compile_graph(g, (EventType("int"),)))
    c.handle(123)
    c.flush()
    assert c.access("counter").count() == 1


def test_execute_rejects_events_and_flush_when_expired():
    g = Graph()
    g.add_node("a", NullSink())
    c = create_execution_context(compile_graph(g, (EventType("int"),)))
    c.flush()
    with pytest.raises(RuntimeError):
        c.handle(123)
    with pytest.raises(RuntimeError):
        c.flush()


def test_execute_handles_buffer_events():
    g = Graph()
    g.add_node("a", NullSink())
    # 'const' not required for span
    c = create_execution_context(
        compile_graph(g, [EventType("span<u8 const>"), EventType("span<i16>")])
    )
    c.handle(b"")
    c.handle(b"abc")
    c.handle(memoryview(b""))
    c.handle(array.array("h", []))
    c.handle(array.array("h", [1, 2, 3]))
    with pytest.raises(TypeError):
        c.handle(array.array("I", [1, 2, 3]))


@pytest.mark.xfail(reason="https://github.com/wlav/cppyy/issues/243")
def test_execute_fails_for_unhandle_events():
    g = Graph()
    g.add_node("s", SinkEvents(EventType("u32")))
    c = create_execution_context(compile_graph(g, ["u32"]))
    c.handle(42)

    # When type is not handled by the actual processor, the error is detected
    # on first calling handle(), not when the (uninstantiated) template code is
    # compiled.
    c2 = create_execution_context(compile_graph(g, [EventType("std::string")]))
    with pytest.raises(SyntaxError):
        c2.handle("abc")
