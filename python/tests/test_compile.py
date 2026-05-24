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
from libtcspc._execute import ExecutionContext
from libtcspc._graph import Graph, Subgraph
from libtcspc._param import Param
from libtcspc._processors import (
    Batch,
    Count,
    ReadBinaryStream,
    SelectAll,
    SinkAll,
    SinkOnly,
    SourceNothing,
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
    g.add_node("a", SinkAll())
    g.add_node("b", SinkAll())
    with pytest.raises(ValueError):
        CompiledGraph(g)


def test_compile_graph_with_single_input_allowed():
    g = Graph()
    g.add_node("a", SinkAll())
    CompiledGraph(g)


def test_compile_graph_with_input_and_output_allowed():
    g = Graph()
    g.add_node("a", SourceNothing())
    CompiledGraph(g)


def test_compile_node_access():
    g = Graph()
    counter = Count(IntEvent, AccessTag("counter"))
    g.add_node("c", counter)
    g.add_node("s", SinkAll(), upstream="c")
    cg = CompiledGraph(g)
    assert len(cg._accesses()) == 1
    assert cg._accesses()[0] == AccessTag("counter")


def test_compile_two_tags_same_access_type():
    g = Graph()
    g.add_node("c1", Count(IntEvent, AccessTag("a")))
    g.add_node("c2", Count(IntEvent, AccessTag("b")), upstream="c1")
    g.add_node("s", SinkAll(), upstream="c2")
    cg = CompiledGraph(g, (IntEvent,))
    assert {t.tag for t in cg._accesses()} == {"a", "b"}
    ec = ExecutionContext(cg, {})
    ec.handle(1)
    assert ec.access(AccessTag("a")).count() == 1
    assert ec.access(AccessTag("b")).count() == 1


def test_compile_subgraph_access_propagates():
    inner = Graph()
    inner.add_node("c", Count(IntEvent, AccessTag("inner_tag")))
    outer = Graph()
    outer.add_node("sg", Subgraph(inner))
    outer.add_node("s", SinkAll(), upstream=("sg", "c:output"))
    cg = CompiledGraph(outer, (IntEvent,))
    assert AccessTag("inner_tag") in cg._accesses()
    ec = ExecutionContext(cg, {})
    ec.handle(1)
    ec.flush()
    assert ec.access(AccessTag("inner_tag")).count() == 1


def test_compile_duplicate_access_tag_in_subgraph_raises():
    inner = Graph()
    inner.add_node("c_inner", Count(IntEvent, AccessTag("dup")))
    sg = Subgraph(
        inner,
        input_map={"input": ("c_inner", "input")},
        output_map={"output": ("c_inner", "output")},
    )
    outer = Graph()
    outer.add_node("c_outer", Count(IntEvent, AccessTag("dup")))
    outer.add_node("sg", sg, upstream="c_outer")
    outer.add_node("s", SinkAll(), upstream="sg")
    with pytest.raises(ValueError, match="dup"):
        CompiledGraph(outer, (IntEvent,))


def test_compile_fails_for_unhandle_events():
    g = Graph()
    g.add_node("s", SinkOnly(_NamedEvent(_uint32_type)))
    with pytest.raises(ValueError):
        CompiledGraph(g, [_NamedEvent(_string_type)])


def test_compile_rejects_unsupported_output_event_type():
    g = Graph()
    g.add_node("r", SelectAll())
    with pytest.raises(TypeError, match="int"):
        CompiledGraph(g, (IntEvent,))


def test_compile_string_parameter():
    g = Graph()
    g.add_node("a", Stop((), "a_default"))
    assert len(g._parameters()) == 0
    g.add_node("b", Stop((), Param("b_msg")), upstream="a")
    assert len(g._parameters()) == 1
    assert g._parameters()[0] == (Param("b_msg"), _string_type)
    g.add_node("c", Stop((), Param("c_msg", "c_default")), upstream="b")
    g.add_node("sink", SinkAll(), upstream="c")
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
    g.add_node("sink", SinkAll(), upstream="b")
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
    g.add_node("sink", SinkAll(), upstream="r")
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
    g.add_node("sink", SinkAll(), upstream="b")
    cg = CompiledGraph(g)
    assert cg.parameters() == (Param("only_param"),)


def test_compile_param_count_zero():
    g = Graph()
    g.add_node("sink", SinkAll())
    cg = CompiledGraph(g)
    assert cg.parameters() == ()
