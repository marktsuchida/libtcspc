# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT


import pytest
from libtcspc._codegen import CodeGenerationContext
from libtcspc._cpp_utils import (
    CppExpression,
    CppIdentifier,
    CppTypeName,
    run_cpp_prog,
)
from libtcspc._events import EventType
from libtcspc._graph import Graph, Node

ShortEvent = EventType(CppTypeName("short"))
IntEvent = EventType(CppTypeName("int"))
LongEvent = EventType(CppTypeName("long"))

gencontext = CodeGenerationContext(
    CppIdentifier("ctx"), CppIdentifier("params"), CppIdentifier("sinks")
)


def test_empty_graph():
    g = Graph()
    assert g._inputs() == ()
    assert g.inputs() == ()
    assert g._outputs() == ()
    assert g.outputs() == ()

    assert g.map_event_sets([]) == ()

    with pytest.raises(ValueError):
        g.map_event_sets([(IntEvent,)])

    code = g.cpp_expression(gencontext)
    # An empty graph has no inputs, so generates a lambda that returns an empty
    # tuple. Assignment should succeed.
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

    with pytest.raises(ValueError):
        g.cpp_expression(gencontext, [CppExpression("downstream")])


def test_single_node(mocker):
    g = Graph()
    node = Node()
    assert g.add_node("node", node) == "node"
    assert g._inputs() == ((0, 0),)
    assert g.inputs() == (("node", "input"),)
    assert g._outputs() == ((0, 0),)
    assert g.outputs() == (("node", "output"),)

    with pytest.raises(ValueError):
        g.map_event_sets([])

    node.map_event_sets = mocker.MagicMock(return_value=((LongEvent,),))  # type: ignore
    assert g.map_event_sets([(IntEvent,)]) == ((LongEvent,),)
    node.map_event_sets.assert_called_with([(IntEvent,)])  # type: ignore

    with pytest.raises(ValueError):
        g.cpp_expression(gencontext)

    def node_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    node.cpp_expression = mocker.MagicMock(side_effect=node_codegen)  # type: ignore
    code = g.cpp_expression(gencontext, [CppExpression("std::move(dstream)")])
    # The generated lambda should return a single-element tuple whose element
    # was moved from 'ds'.
    assert (
        run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>
            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int dstream = 42; // Fake the downstream processor with int
                auto proc = {code};
                static_assert(std::is_same_v<decltype(proc), int>);
                return proc;
            }}
            int main() {{ return f(); }}
            """)
        == 42
    )


def test_two_nodes_two_inputs_two_outputs(mocker):
    # in0                               out0 (short)
    #     \                           /
    #      +- node0 --(int)-- node1 -+
    #     /                           \
    # in1                               out1 (long)

    g = Graph()

    node0 = Node(input=["in0", "in1"])
    node1 = Node(output=["out0", "out1"])
    assert g.add_node("n0", node0) == "n0"
    assert g.add_node("n1", node1) == "n1"

    node0.map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    node1.map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((ShortEvent,), (LongEvent,))
    )
    g.connect("n0", "n1")
    node0.map_event_sets.assert_called_with([(), ()])  # type: ignore
    node1.map_event_sets.assert_called_with([(IntEvent,)])  # type: ignore

    def node0_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return f"std::tuple{{2 * {downstreams[0]}, 123}}"

    def node1_codegen(gencontext, downstreams):
        assert len(downstreams) == 2
        return f"5 * ({downstreams[0]} + {downstreams[1]})"

    node0.cpp_expression = mocker.MagicMock(side_effect=node0_codegen)  # type: ignore
    node1.cpp_expression = mocker.MagicMock(side_effect=node1_codegen)  # type: ignore
    code = g.cpp_expression(
        gencontext,
        [CppExpression("std::move(ds0)"), CppExpression("std::move(ds1)")],
    )
    assert (
        run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <string>
            #include <tuple>
            #include <type_traits>
            #include <utility>

            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int ds0 = 42, ds1 = 43;
                auto [p0, p1] = {code};
                static_assert(std::is_same_v<decltype(p0), int>);
                static_assert(std::is_same_v<decltype(p1), int>);
                // Work around structured binding limitations:
                return std::tuple(p0, p1);
            }}

            int main(int argc, char *argv[]) {{
                auto procs = f();
                auto proc0 = std::get<0>(procs);
                auto proc1 = std::get<1>(procs);
                return proc0 == 2 * (5 * (42 + 43)) && proc1 == 123 ? 0 : 1;
            }}
            """)
        == 0
    )


def test_two_nodes_two_internal_edges(mocker):
    #                    --(long )--
    #                  /             \
    # input -- node0 -+               +- node1 -- output (int)
    #                  \             /
    #                    --(short)--

    g = Graph()

    node0 = Node(output=["out0", "out1"])
    node1 = Node(input=["in0", "in1"])
    assert g.add_node("n0", node0) == "n0"
    assert g.add_node("n1", node1) == "n1"

    node0.map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((LongEvent,), (ShortEvent,))
    )
    node1.map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    with pytest.raises(ValueError):
        g.connect("n0", "n1")
    g.connect(("n0", "out0"), ("n1", "in0"))
    g.connect(("n0", "out1"), ("n1", "in1"))
    node0.map_event_sets.assert_called_with([()])  # type: ignore
    node1.map_event_sets.assert_called_with([(LongEvent,), (ShortEvent,)])  # type: ignore

    def node0_codegen(gencontext, downstreams):
        assert len(downstreams) == 2
        return f"{downstreams[0]} + {downstreams[1]}"

    def node1_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return f"std::tuple{{2 * {downstreams[0]}, 123}}"

    node0.cpp_expression = mocker.MagicMock(side_effect=node0_codegen)  # type: ignore
    node1.cpp_expression = mocker.MagicMock(side_effect=node1_codegen)  # type: ignore
    code = g.cpp_expression(gencontext, [CppExpression("std::move(ds)")])
    assert (
        run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>
            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int ds = 42;
                auto proc = {code};
                static_assert(std::is_same_v<decltype(proc), int>);
                return proc;
            }}
            int main() {{ return f(); }}
            """)
        == 2 * 42 + 123
    )


def make_simple_node(mocker) -> Node:
    def node_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    node = Node()
    node.map_event_sets = mocker.MagicMock(return_value=(IntEvent,))  # type: ignore
    node.cpp_expression = mocker.MagicMock(side_effect=node_codegen)  # type: ignore
    return node


def test_add_sequence(mocker):
    node0 = make_simple_node(mocker)
    node1 = make_simple_node(mocker)
    node2 = make_simple_node(mocker)
    node3 = make_simple_node(mocker)
    node4 = make_simple_node(mocker)

    g = Graph()
    g.add_sequence([node0, node1])
    g.add_sequence([node2, node3], upstream="Node-1")
    g.add_sequence([node4], downstream=("Node-0", "input"))
    code = g.cpp_expression(gencontext, [CppExpression("std::move(ds)")])
    assert (
        run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>
            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int ds = 42;
                auto proc = {code};
                static_assert(std::is_same_v<decltype(proc), int>);
                return proc;
            }}
            int main() {{ return f(); }}
            """)
        == 42
    )


def test_add_sequence_with_cycle(mocker):
    node0 = make_simple_node(mocker)
    node1 = make_simple_node(mocker)
    node2 = make_simple_node(mocker)
    node3 = make_simple_node(mocker)
    node4 = make_simple_node(mocker)

    g = Graph()
    g.add_sequence([node0, node1, node2])
    with pytest.raises(ValueError):
        # Would introduce cycle
        g.add_sequence([node3, node4], upstream="Node-2", downstream="Node-0")


def test_add_sequence_not_single_input_output(mocker):
    g = Graph()
    n0 = Node(input=["in0", "in1"])
    with pytest.raises(ValueError):
        g.add_sequence([n0])
    n1 = Node(output=["out0", "out1"])
    with pytest.raises(ValueError):
        g.add_sequence([n1])


def test_empty_add_sequence_connects_upstream_downstream(mocker):
    g = Graph()
    n0 = Node()
    n1 = Node()
    g.add_node("n0", n0)
    g.add_node("n1", n1)
    n0.map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    n1.map_event_sets = mocker.MagicMock(return_value=((LongEvent,),))  # type: ignore
    g.add_sequence([], upstream="n0", downstream="n1")
    n0.map_event_sets.assert_called_with([()])  # type: ignore
    n1.map_event_sets.assert_called_with([(IntEvent,)])  # type: ignore
    with pytest.raises(ValueError):
        # Would introduce cycle
        g.add_sequence([], upstream="n1", downstream="n0")
