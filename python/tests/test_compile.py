# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import cppyy
import pytest
from libtcspc._access import AccessTag
from libtcspc._compile import compile_graph
from libtcspc._events import EventType
from libtcspc._graph import Graph
from libtcspc._processors import CheckMonotonic, Count, NullSink


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
    cg = compile_graph(g)
    p = cg._instantiator(cppyy.gbl.tcspc.context.create(), cg._param_struct())
    p.flush()


def test_compile_node_access():
    g = Graph()
    counter = Count(EventType("int"), AccessTag("counter"))
    g.add_node("c", counter)
    g.add_node("s", NullSink(), upstream="c")
    cg = compile_graph(g)
    assert cg.access_types()["counter"] is counter.accesses()[0][1]
