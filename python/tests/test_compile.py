# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._access import AccessTag
from libtcspc._compile import compile_graph
from libtcspc._cpp_utils import CppTypeName
from libtcspc._events import EventType
from libtcspc._graph import Graph
from libtcspc._processors import CheckMonotonic, Count, NullSink

IntEvent = EventType(CppTypeName("int"))


def test_compile_empty_graph_rejected():
    g = Graph()
    with pytest.raises(ValueError):
        compile_graph(g)


def test_compile_graph_with_two_inputs_rejected():
    g = Graph()
    g.add_node("a", NullSink())
    g.add_node("b", NullSink())
    with pytest.raises(ValueError):
        compile_graph(g)


def test_compile_graph_with_output_rejected():
    g = Graph()
    g.add_node("a", CheckMonotonic())
    with pytest.raises(ValueError):
        compile_graph(g)


def test_compile_graph_with_single_input_allowed():
    g = Graph()
    g.add_node("a", NullSink())
    compile_graph(g)


def test_compile_node_access():
    g = Graph()
    counter = Count(IntEvent, AccessTag("counter"))
    g.add_node("c", counter)
    g.add_node("s", NullSink(), upstream="c")
    cg = compile_graph(g)
    assert len(cg.accesses()) == 1
    assert cg.accesses()[0] == "counter"
