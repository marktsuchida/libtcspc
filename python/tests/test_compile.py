# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import Any

import cppyy
import pytest
from libtcspc._access import AccessTag
from libtcspc._compile import (
    _collect_access_tags,
    _collect_params,
    compile_graph,
)
from libtcspc._cpp_utils import CppIdentifier, CppTypeName
from libtcspc._events import EventType
from libtcspc._graph import Graph
from libtcspc._node import RelayNode
from libtcspc._processors import CheckMonotonic, Count, NullSink

IntEvent = EventType(CppTypeName("int"))


def test_compile_collect_params_duplicate():
    class ParamNode(RelayNode):
        def __init__(self, param_name: str) -> None:
            self._param_name = CppIdentifier(param_name)

        def parameters(
            self,
        ) -> tuple[tuple[CppIdentifier, CppTypeName, Any], ...]:
            return ((self._param_name, CppTypeName("int"), None),)

    g = Graph()
    g.add_node("a", ParamNode("hello"))
    g.add_node("b", ParamNode("world"))
    assert len(_collect_params(g)) == 2
    g.add_node("c", ParamNode("hello"))
    with pytest.raises(ValueError, match="param.*hello"):
        _collect_params(g)


def test_compile_collect_access_tags_duplicate():
    g = Graph()
    g.add_node("a", Count(IntEvent, AccessTag("hello")))
    g.add_node("b", Count(IntEvent, AccessTag("world")))
    assert len(_collect_access_tags(g)) == 2
    g.add_node("c", Count(IntEvent, AccessTag("hello")))
    with pytest.raises(ValueError, match="tag.*hello"):
        _collect_access_tags(g)


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
    counter = Count(IntEvent, AccessTag("counter"))
    g.add_node("c", counter)
    g.add_node("s", NullSink(), upstream="c")
    cg = compile_graph(g)
    assert len(cg.access_types()) == 1
    assert cg.access_types()[0] == ("counter", counter.accesses()[0][1])
