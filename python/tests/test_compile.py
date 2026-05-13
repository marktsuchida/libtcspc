# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._access import AccessTag
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppTypeName,
    _size_type,
    _string_type,
    _uint32_type,
    _uint64_type,
)
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import (
    Batch,
    Count,
    NullSink,
    NullSource,
    ReadBinaryStream,
    SinkEvents,
    Stop,
)
from libtcspc._streams import BinaryFileInputStream

IntEvent = _NamedEvent(_CppTypeName("int"))


def test_compile_empty_graph_rejected():
    g = Graph()
    with pytest.raises(ValueError):
        CompiledGraph(g)


def test_compile_graph_with_two_inputs_rejected():
    g = Graph()
    g.add_node("a", NullSink())
    g.add_node("b", NullSink())
    with pytest.raises(ValueError):
        CompiledGraph(g)


def test_compile_graph_with_single_input_allowed():
    g = Graph()
    g.add_node("a", NullSink())
    CompiledGraph(g)


def test_compile_graph_with_input_and_output_allowed():
    g = Graph()
    g.add_node("a", NullSource())
    CompiledGraph(g)


def test_compile_node_access():
    g = Graph()
    counter = Count(IntEvent, AccessTag("counter"))
    g.add_node("c", counter)
    g.add_node("s", NullSink(), upstream="c")
    cg = CompiledGraph(g)
    assert len(cg._accesses()) == 1
    assert cg._accesses()[0] == AccessTag("counter")


def test_compile_fails_for_unhandle_events():
    g = Graph()
    g.add_node("s", SinkEvents(_NamedEvent(_uint32_type)))
    with pytest.raises(ValueError):
        CompiledGraph(g, [_NamedEvent(_string_type)])


def test_compile_string_parameter():
    g = Graph()
    g.add_node("a", Stop((), "a_default"))
    assert len(g._parameters()) == 0
    g.add_node("b", Stop((), Param("b_msg")), upstream="a")
    assert len(g._parameters()) == 1
    assert g._parameters()[0] == (Param("b_msg"), _string_type)
    g.add_node("c", Stop((), Param("c_msg", "c_default")), upstream="b")
    g.add_node("sink", NullSink(), upstream="c")
    cg = CompiledGraph(g)
    assert len(cg.parameters()) == 2
    assert set(p.name for p in cg.parameters()) == {"b_msg", "c_msg"}
    b_param = list(p for p in cg.parameters() if p.name == "b_msg")[0]
    c_param = list(p for p in cg.parameters() if p.name == "c_msg")[0]
    assert b_param.default_value is None
    assert c_param.default_value == "c_default"


def test_compile_int_parameter():
    g = Graph()
    g.add_node("b", Batch(IntEvent, batch_size=Param("bs")))
    g.add_node("sink", NullSink(), upstream="b")
    assert g._parameters()[0] == (Param("bs"), _size_type)
    cg = CompiledGraph(g, (IntEvent,))
    assert cg.parameters() == (Param("bs"),)


def test_compile_multiple_params_same_node():
    g = Graph()
    g.add_node(
        "r",
        ReadBinaryStream(
            IntEvent,
            BinaryFileInputStream("some_file"),
            max_length=Param("ml"),
            read_granularity_bytes=Param("rg"),
        ),
    )
    g.add_node("sink", NullSink(), upstream="r")
    cg = CompiledGraph(g)
    assert {p.name for p in cg.parameters()} == {"ml", "rg"}
    by_name = {p.name: p for p in cg.parameters()}
    assert by_name["ml"].default_value is None
    assert by_name["rg"].default_value is None
    # Verify the declared C++ types match what the node reports.
    types_by_name = {p.name: t for p, t in g._parameters()}
    assert types_by_name["ml"] == _uint64_type
    assert types_by_name["rg"] == _size_type


def test_compile_mixes_concrete_and_param():
    g = Graph()
    g.add_node("a", Stop((), "concrete-prefix"))
    g.add_node("b", Stop((), Param("only_param")), upstream="a")
    g.add_node("sink", NullSink(), upstream="b")
    cg = CompiledGraph(g)
    assert cg.parameters() == (Param("only_param"),)


def test_compile_param_count_zero():
    g = Graph()
    g.add_node("sink", NullSink())
    cg = CompiledGraph(g)
    assert cg.parameters() == ()
