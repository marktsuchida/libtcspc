# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _TestNode, _TestRelayNode
from libtcspc._graph import Graph, Subgraph


def test_empty_graph():
    g = Graph()
    dot = g.to_graphviz()
    assert dot.startswith("digraph {")
    assert dot.rstrip().endswith("}")
    assert "->" not in dot
    assert "shape=box" not in dot
    assert "cluster_" not in dot


def test_single_node():
    g = Graph()
    g.add_node("n", _TestRelayNode())
    dot = g.to_graphviz()
    assert "_TestRelayNode" in dot
    assert "\\nn" in dot
    assert "shape=box" in dot


def test_chain_of_nodes(mocker):
    g = Graph()
    a = _TestRelayNode()
    b = _TestRelayNode()
    c = _TestRelayNode()
    a._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    b._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    c._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    g.add_node("a", a)
    g.add_node("b", b, upstream="a")
    g.add_node("c", c, upstream="b")
    dot = g.to_graphviz()
    # Three nodes (n0, n1, n2) and two edges between them.
    assert "n0 -> n1" in dot
    assert "n1 -> n2" in dot


def test_multi_port_node_labels(mocker):
    # A multi-input node has headlabels on incoming edges; downstream
    # single-input node has no headlabel.
    g = Graph()
    a = _TestRelayNode()
    b = _TestRelayNode()
    multi = _TestNode(input=("input-0", "input-1"))
    sink = _TestRelayNode()
    a._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    b._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    multi._map_event_sets = mocker.MagicMock(return_value=((),))  # type: ignore
    sink._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    g.add_node("a", a)
    g.add_node("b", b)
    g.add_node("multi", multi)
    g.add_node("sink", sink, upstream="multi")
    g.connect("a", ("multi", "input-0"))
    g.connect("b", ("multi", "input-1"))
    dot = g.to_graphviz()
    assert 'headlabel="input-0"' in dot
    assert 'headlabel="input-1"' in dot
    # The edge from multi to sink should not carry a headlabel (sink has
    # a single input).
    multi_to_sink_line = next(
        ln
        for ln in dot.splitlines()
        if "->" in ln and ln.strip().split()[0] == "n2"
    )
    assert "headlabel" not in multi_to_sink_line


def test_multi_output_node_taillabels(mocker):
    g = Graph()
    src = _TestNode(output=("output-0", "output-1"))
    a = _TestRelayNode()
    b = _TestRelayNode()
    src._map_event_sets = mocker.MagicMock(return_value=((), ()))  # type: ignore
    a._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    b._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    g.add_node("src", src)
    g.add_node("a", a)
    g.add_node("b", b)
    g.connect(("src", "output-0"), "a")
    g.connect(("src", "output-1"), "b")
    dot = g.to_graphviz()
    assert 'taillabel="output-0"' in dot
    assert 'taillabel="output-1"' in dot


def test_external_input_output_markers():
    g = Graph()
    g.add_node("n", _TestRelayNode())
    dot = g.to_graphviz()
    assert "shape=point" in dot
    assert 'xlabel="n:input"' in dot
    assert 'xlabel="n:output"' in dot


def test_subgraph_default_renders_cluster():
    inner = Graph()
    inner.add_node("inner_node", _TestRelayNode())
    sg = Subgraph(inner)
    g = Graph()
    g.add_node("sg", sg)
    dot = g.to_graphviz()
    assert "subgraph cluster_" in dot
    assert "_TestRelayNode" in dot
    assert "\\ninner_node" in dot
    # The Subgraph itself should not appear as a labeled box.
    assert "\\nsg" not in dot


def test_subgraph_default_outer_edge_targets_inner_node(mocker):
    inner = Graph()
    inner_node = _TestRelayNode()
    inner_node._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    inner.add_node("inner_node", inner_node)
    sg = Subgraph(inner, input_map={"input": ("inner_node", "input")})

    outer = Graph()
    outside = _TestRelayNode()
    outside._relay_map_event_set = mocker.MagicMock(return_value=())  # type: ignore
    outer.add_node("outside", outside)
    outer.add_node("sg", sg, upstream="outside")
    dot = outer.to_graphviz()
    # Edge goes from outside (n0) to the inner node directly. The Subgraph
    # node "sg" got id n1, the inner node got id n2.
    assert "n0 -> n2" in dot


def test_subgraph_flatten():
    inner = Graph()
    inner.add_node("inner_node", _TestRelayNode())
    sg = Subgraph(inner)
    g = Graph()
    g.add_node("sg", sg)
    dot = g.to_graphviz(flatten=True)
    assert "cluster_" not in dot
    assert "_TestRelayNode" in dot
    assert "\\ninner_node" in dot


def test_nested_subgraph():
    innermost = Graph()
    innermost.add_node("leaf", _TestRelayNode())
    sg_inner = Subgraph(innermost)
    middle = Graph()
    middle.add_node("sg_inner", sg_inner)
    sg_outer = Subgraph(middle)
    outer = Graph()
    outer.add_node("sg_outer", sg_outer)
    dot = outer.to_graphviz()
    # Two nested cluster blocks should appear.
    assert dot.count("subgraph cluster_") == 2
    assert "\\nleaf" in dot


def test_subgraph_no_external_markers_inside_cluster():
    inner = Graph()
    inner.add_node("inner_node", _TestRelayNode())
    sg = Subgraph(inner)
    g = Graph()
    g.add_node("sg", sg)
    dot = g.to_graphviz()
    # Only the outer graph's external ports become markers; inner ports
    # are reached via the cluster boundary edges.
    assert dot.count("shape=point") == 2
    assert 'xlabel="sg:' in dot
