# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import array

import cppyy
import pytest
from libtcspc._context import Context
from libtcspc._events import EventType
from libtcspc._graph import Graph
from libtcspc._processors import Count, NullSink, SinkEvents

cppyy.include("string")


def test_context_empty_graph_rejected():
    g = Graph()
    with pytest.raises(ValueError):
        Context(g)


def test_context_graph_with_two_inputs_rejected():
    g = Graph()
    g.add_node("a", NullSink())
    g.add_node("b", NullSink())
    with pytest.raises(ValueError):
        Context(g)


def test_context_graph_with_output_rejected():
    g = Graph()
    g.add_node("a", Count(EventType("int")))
    with pytest.raises(ValueError):
        Context(g)


def test_context_graph_with_single_input_allowed():
    g = Graph()
    g.add_node("a", NullSink())
    c = Context(g, ("int",))
    c.handle(123)
    c.flush()

    g.add_node("c", Count(EventType("int")), downstream="a")
    c = Context(g, ("int",))
    c.handle(123)
    c.flush()


def test_context_rejects_events_and_flush_when_expired():
    g = Graph()
    g.add_node("a", NullSink())
    c = Context(g, ("int",))
    c.flush()
    with pytest.raises(RuntimeError):
        c.handle(123)
    with pytest.raises(RuntimeError):
        c.flush()


def test_context_handles_buffer_events():
    g = Graph()
    g.add_node("a", NullSink())
    c = Context(g, ["span<u8 const>", "span<i16>"])  # 'const' not required
    c.handle(b"")
    c.handle(b"abc")
    c.handle(memoryview(b""))
    c.handle(array.array("h", []))
    c.handle(array.array("h", [1, 2, 3]))
    with pytest.raises(TypeError):
        c.handle(array.array("I", [1, 2, 3]))


@pytest.mark.xfail(reason="https://github.com/wlav/cppyy/issues/243")
def test_context_fails_for_unhandle_events():
    g = Graph()
    g.add_node("s", SinkEvents(EventType("u32")))
    c = Context(g, ["u32"])
    c.handle(42)

    # When type is not handled by the actual processor, the error is detected
    # on first calling handle(), not when the (uninstantiated) template code is
    # compiled.
    c2 = Context(g, ["std::string"])
    with pytest.raises(SyntaxError):
        c2.handle("abc")
