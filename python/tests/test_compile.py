# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._access import AccessTag
from libtcspc._compile import compile_graph
from libtcspc._cpp_utils import CppIdentifier, CppTypeName
from libtcspc._events import EventType
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import (
    CheckMonotonic,
    Count,
    NullSink,
    SinkEvents,
    Stop,
)

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
    assert cg.accesses()[0] == AccessTag("counter")


def test_compile_fails_for_unhandle_events():
    g = Graph()
    g.add_node("s", SinkEvents(EventType(CppTypeName("tcspc::u32"))))
    with pytest.raises(RuntimeError):
        compile_graph(g, [EventType(CppTypeName("std::string"))])


def test_compile_string_parameter():
    g = Graph()
    g.add_node("a", Stop((), "a_default"))
    assert len(g.parameters()) == 0
    g.add_node("b", Stop((), Param(CppIdentifier("b_msg"))), upstream="a")
    assert len(g.parameters()) == 1
    assert g.parameters()[0] == (
        Param(CppIdentifier("b_msg")),
        CppTypeName("std::string"),
    )
    g.add_node(
        "c",
        Stop((), Param(CppIdentifier("c_msg"), "c_default")),
        upstream="b",
    )
    g.add_node("sink", NullSink(), upstream="c")
    cg = compile_graph(g)
    assert len(cg.parameters()) == 2
    assert set(p.name for p in cg.parameters()) == {"b_msg", "c_msg"}
    b_param = list(p for p in cg.parameters() if p.name == "b_msg")[0]
    c_param = list(p for p in cg.parameters() if p.name == "c_msg")[0]
    assert b_param.default_value is None
    assert c_param.default_value == "c_default"
