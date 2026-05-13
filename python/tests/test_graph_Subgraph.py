# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence

import pytest
from _test_helpers import _TestNode, _TestRelayNode
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _run_cpp_prog,
)
from libtcspc._graph import Graph, Subgraph
from libtcspc._param import Param


class _ParamNode(_TestRelayNode):
    def __init__(self, param_name: str, default: object = None) -> None:
        self._param_name = param_name
        self._default = default

    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return ((Param(self._param_name, self._default), _CppTypeName("int")),)


gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_empty_subgraph():
    g = Graph()
    sg = Subgraph(g)
    assert sg.inputs() == ()
    assert sg.outputs() == ()

    assert sg._map_event_sets([]) == ()

    code = sg._cpp_expression(gencontext, [])
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <tuple>
            int main() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                std::tuple<> t = {code};
                return 0;
            }}
            """)
        == 0
    )


def test_input_output_map():
    g = Graph()
    node = _TestNode()
    g.add_node("n", node)

    sg = Subgraph(g)
    assert sg.inputs() == ("n:input",)
    assert sg.outputs() == ("n:output",)

    sg = Subgraph(
        g,
        input_map={"renamed_input": ("n", "input")},
        output_map={"renamed_output": ("n", "output")},
    )
    assert sg.inputs() == ("renamed_input",)
    assert sg.outputs() == ("renamed_output",)


def test_subgraph_with_no_parameters():
    g_empty = Graph()
    assert Subgraph(g_empty)._parameters() == []

    g = Graph()
    g.add_node("n", _TestRelayNode())
    assert Subgraph(g)._parameters() == []


def test_subgraph_propagates_parameters():
    inner = Graph()
    inner.add_node("n", _ParamNode("p", "default"))
    sg = Subgraph(inner)
    params = sg._parameters()
    assert len(params) == 1
    assert params[0] == (Param("p", "default"), _CppTypeName("int"))


def test_subgraph_nested_parameter_propagation():
    inner = Graph()
    inner.add_node("n", _ParamNode("p"))
    sg_inner = Subgraph(inner)
    middle = Graph()
    middle.add_node("sg_inner", sg_inner)
    sg_outer = Subgraph(middle)
    params = sg_outer._parameters()
    assert len(params) == 1
    assert params[0] == (Param("p"), _CppTypeName("int"))


def test_subgraph_param_collides_with_outer_node_param():
    inner = Graph()
    inner.add_node("n_inner", _ParamNode("x"))
    sg = Subgraph(inner)

    outer = Graph()
    outer.add_node("n_outer", _ParamNode("x"))
    outer.add_node("sg", sg)
    with pytest.raises(ValueError, match="param.*x"):
        outer._parameters()


def test_nested_subgraph(mocker):
    node = _TestNode()

    def node_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    # Add mock to node before creating subgraphs, so that the mock method is
    # copied when the graph is copied. (Not ideal because we cannot check that
    # it is called, but here we are mainly interested in testing the codegen
    # result.)
    node._cpp_expression = mocker.MagicMock(side_effect=node_codegen)  # type: ignore

    g0 = Graph()
    g0.add_node("n", node)
    sg0 = Subgraph(g0)
    g1 = Graph()
    g1.add_node("sg0", sg0)
    sg1 = Subgraph(g1)

    code = sg1._cpp_expression(
        gencontext, [_CppExpression("std::move(dstream)")]
    )
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>
            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int dstream = 42;
                auto proc = {code};
                static_assert(std::is_same_v<decltype(proc), int>);
                return proc;
            }}
            int main() {{ return f(); }}
            """)
        == 42
    )
