# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._codegen import CodeGenerationContext
from libtcspc._cpp_utils import CppExpression, CppIdentifier, run_cpp_prog
from libtcspc._graph import Graph, Node, Subgraph

gencontext = CodeGenerationContext(
    CppIdentifier("ctx"), CppIdentifier("params")
)


def test_empty_subgraph():
    g = Graph()
    sg = Subgraph(g)
    assert sg.inputs() == ()
    assert sg.outputs() == ()

    assert sg.map_event_sets([]) == ()

    code = sg.cpp_expression(gencontext, [])
    assert (
        run_cpp_prog(f"""\
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
    node = Node()
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


def test_nested_subgraph(mocker):
    node = Node()

    def node_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    # Add mock to node before creating subgraphs, so that the mock method is
    # copied when the graph is copied. (Not ideal because we cannot check that
    # it is called, but here we are mainly interested in testing the codegen
    # result.)
    node.cpp_expression = mocker.MagicMock(side_effect=node_codegen)  # type: ignore

    g0 = Graph()
    g0.add_node("n", node)
    sg0 = Subgraph(g0)
    g1 = Graph()
    g1.add_node("sg0", sg0)
    sg1 = Subgraph(g1)

    code = sg1.cpp_expression(
        gencontext, [CppExpression("std::move(dstream)")]
    )
    assert (
        run_cpp_prog(f"""\
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
