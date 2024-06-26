# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT


import cppyy
import pytest
from cpp_utils import isolated_cppdef
from libtcspc._events import EventType
from libtcspc._graph import Graph, Node

cppyy.include("memory")
cppyy.include("tuple")
cppyy.include("type_traits")
cppyy.include("utility")


def test_empty_graph():
    g = Graph()
    assert g._inputs() == ()
    assert g.inputs() == ()
    assert g._outputs() == ()
    assert g.outputs() == ()

    assert g.map_event_sets([]) == ()

    with pytest.raises(ValueError):
        g.map_event_sets([EventType("int")])

    code = g.generate_cpp("g", "ctx", [])
    # An empty graph has no inputs, so generates a lambda that returns an empty
    # tuple. Assignment should succeed.
    isolated_cppdef(f"""\
        auto ctx = tcspc::context::create();
        std::tuple<> t = {code};
    """)

    with pytest.raises(ValueError):
        g.generate_cpp("g", "ctx", ["downstream"])


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

    node.map_event_sets = mocker.MagicMock(
        return_value=((EventType("long"),),)
    )
    assert g.map_event_sets([(EventType("int"),)]) == ((EventType("long"),),)
    node.map_event_sets.assert_called_with([(EventType("int"),)])

    with pytest.raises(ValueError):
        g.generate_cpp("g", "ctx", [])

    def node_codegen(name, context, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    node.generate_cpp = mocker.MagicMock(side_effect=node_codegen)
    code = g.generate_cpp("g", "ctx", ["std::move(dstream)"])
    # The generated lambda should return a single-element tuple whose element
    # was moved from 'ds'.
    ns = isolated_cppdef(f"""\
        auto ctx = tcspc::context::create();
        int dstream = 42; // Fake the downstream processor with int
        auto proc = {code};
        static_assert(std::is_same_v<decltype(proc), int>);
    """)
    assert ns.proc == 42


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

    node0.map_event_sets = mocker.MagicMock(
        return_value=((EventType("int"),),)
    )
    node1.map_event_sets = mocker.MagicMock(
        return_value=((EventType("short"),), (EventType("long"),))
    )
    g.connect("n0", "n1")
    node0.map_event_sets.assert_called_with([(), ()])
    node1.map_event_sets.assert_called_with([(EventType("int"),)])

    def node0_codegen(name, context, downstreams):
        assert len(downstreams) == 1
        return f"std::tuple{{2 * {downstreams[0]}, 123}}"

    def node1_codegen(name, context, downstreams):
        assert len(downstreams) == 2
        return f"5 * ({downstreams[0]} + {downstreams[1]})"

    node0.generate_cpp = mocker.MagicMock(side_effect=node0_codegen)
    node1.generate_cpp = mocker.MagicMock(side_effect=node1_codegen)
    code = g.generate_cpp("g", "ctx", ["std::move(ds0)", "std::move(ds1)"])
    ns = isolated_cppdef(f"""\
        auto ctx = tcspc::context::create();
        int ds0 = 42, ds1 = 43;
        auto [p0, p1] = {code};
        static_assert(std::is_same_v<decltype(p0), int>);
        static_assert(std::is_same_v<decltype(p1), int>);
        // Work around structured binding limitations:
        auto proc0 = p0;
        auto proc1 = p1;
    """)
    assert ns.proc0 == 2 * (5 * (42 + 43))
    assert ns.proc1 == 123


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

    node0.map_event_sets = mocker.MagicMock(
        return_value=((EventType("long"),), (EventType("short"),))
    )
    node1.map_event_sets = mocker.MagicMock(
        return_value=((EventType("int"),),)
    )
    with pytest.raises(ValueError):
        g.connect("n0", "n1")
    g.connect(("n0", "out0"), ("n1", "in0"))
    g.connect(("n0", "out1"), ("n1", "in1"))
    node0.map_event_sets.assert_called_with([()])
    node1.map_event_sets.assert_called_with(
        [(EventType("long"),), (EventType("short"),)]
    )

    def node0_codegen(name, context, downstreams):
        assert len(downstreams) == 2
        return f"{downstreams[0]} + {downstreams[1]}"

    def node1_codegen(name, context, downstreams):
        assert len(downstreams) == 1
        return f"std::tuple{{2 * {downstreams[0]}, 123}}"

    node0.generate_cpp = mocker.MagicMock(side_effect=node0_codegen)
    node1.generate_cpp = mocker.MagicMock(side_effect=node1_codegen)
    code = g.generate_cpp("g", "ctx", ["std::move(ds)"])
    ns = isolated_cppdef(f"""\
        auto ctx = tcspc::context::create();
        int ds = 42;
        auto proc = {code};
        static_assert(std::is_same_v<decltype(proc), int>);
    """)
    assert ns.proc == 2 * 42 + 123


def make_simple_node(mocker):
    def node_codegen(name, context, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    node = Node()
    node.map_event_sets = mocker.MagicMock(return_value=(EventType("int"),))
    node.generate_cpp = mocker.MagicMock(side_effect=node_codegen)
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
    code = g.generate_cpp("g", "ctx", ["std::move(ds)"])
    ns = isolated_cppdef(f"""\
        auto ctx = tcspc::context::create();
        int ds = 42;
        auto proc = {code};
        static_assert(std::is_same_v<decltype(proc), int>);
    """)
    assert ns.proc == 42


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
    n0.map_event_sets = mocker.MagicMock(return_value=((EventType("int"),),))
    n1.map_event_sets = mocker.MagicMock(return_value=((EventType("long"),),))
    g.add_sequence([], upstream="n0", downstream="n1")
    n0.map_event_sets.assert_called_with([()])
    n1.map_event_sets.assert_called_with([(EventType("int"),)])
    with pytest.raises(ValueError):
        # Would introduce cycle
        g.add_sequence([], upstream="n1", downstream="n0")
