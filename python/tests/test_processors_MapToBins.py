# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from libtcspc._access import AccessTag, _UniqueBinMapperAccessSpec
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_MapToBins_event_set():
    node = tcspc.MapToBins(tcspc.LinearBinMapper(0, 1, 100))
    (out,) = node._map_event_sets([(tcspc.DatapointEvent(),)])
    assert out == (tcspc.BinIncrementEvent(),)


def test_MapToBins_codegen():
    node = tcspc.MapToBins(tcspc.LinearBinMapper(0, 1, 100))
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::map_to_bins<tcspc::default_numeric_traits>(" in code
    assert "tcspc::linear_bin_mapper<" in code
    assert "DOWN" in code


def test_MapToBins_forwards_unique_accesses():
    tag = AccessTag("u")
    node = tcspc.MapToBins(tcspc.UniqueBinMapper(tag, 100))
    assert node._accesses() == ((tag, _UniqueBinMapperAccessSpec),)


def test_MapToBins_no_accesses_for_plain_mapper():
    node = tcspc.MapToBins(tcspc.LinearBinMapper(0, 1, 100))
    assert node._accesses() == ()


def test_MapToBins_unique_access_end_to_end():
    from libtcspc._compile import CompiledGraph
    from libtcspc._execute import ExecutionContext
    from libtcspc._graph import Graph

    tag = AccessTag("ubm")
    g = Graph()
    g.add_node("mb", tcspc.MapToBins(tcspc.UniqueBinMapper(tag, 100)))
    g.add_node(None, tcspc.SinkAll(), upstream="mb")
    cg = CompiledGraph(g, (tcspc.DatapointEvent(),))
    ctx = ExecutionContext(cg)
    ctx.flush()
    assert ctx.access(tag).values() == []
