# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._graph import _Edge, _update_topological_order


def test_two_node_already_sorted():
    nodes = [0, 1]
    _update_topological_order(
        nodes,
        {0: (None,), 1: (None,)},
        {0: (None,), 1: (None,)},
        _Edge(0, 1, ()),
    )
    assert nodes == [0, 1]


def test_two_node_already_sorted_edge_preadded():
    nodes = [0, 1]
    edge = _Edge(0, 1, ())
    _update_topological_order(
        nodes,
        {0: (None,), 1: (edge,)},
        {0: (edge,), 1: (None,)},
        edge,
    )
    assert nodes == [0, 1]


def test_two_node_not_sorted():
    nodes = [0, 1]
    _update_topological_order(
        nodes,
        {0: (None,), 1: (None,)},
        {0: (None,), 1: (None,)},
        _Edge(1, 0, ()),
    )
    assert nodes == [1, 0]


def test_two_node_not_sorted_edge_preadded():
    nodes = [0, 1]
    edge = _Edge(1, 0, ())
    _update_topological_order(
        nodes,
        {0: (edge,), 1: (None,)},
        {0: (None,), 1: (edge,)},
        edge,
    )
    assert nodes == [1, 0]


def test_unrelated_node_in_ar():
    nodes = [0, 1, 2]
    edge = _Edge(2, 0, ())
    _update_topological_order(
        nodes,
        {i: (None,) for i in nodes},
        {i: (None,) for i in nodes},
        edge,
    )
    assert nodes == [2, 1, 0]


def test_downstream_node_in_ar():
    nodes = [0, 1, 2]
    edge = _Edge(2, 0, ())
    edge01 = _Edge(0, 1, ())
    _update_topological_order(
        nodes,
        {0: (None,), 1: (edge01,), 2: (None,)},
        {0: (edge01,), 1: (None,), 2: (None,)},
        edge,
    )
    assert nodes == [2, 0, 1]


def test_upstream_node_in_ar():
    nodes = [0, 1, 2]
    edge = _Edge(2, 0, ())
    edge12 = _Edge(1, 2, ())
    _update_topological_order(
        nodes,
        {0: (None,), 1: (None,), 2: (edge12,)},
        {0: (None,), 1: (edge12,), 2: (None,)},
        edge,
    )
    assert nodes == [1, 2, 0]


def test_create_cycle_raises():
    nodes = [0, 1, 2]
    edge = _Edge(2, 0, ())
    edge02 = _Edge(0, 2, ())
    with pytest.raises(ValueError):
        _update_topological_order(
            nodes,
            {0: (None,), 1: (None,), 2: (edge02,)},
            {0: (edge02,), 1: (None,), 2: (None,)},
            edge,
        )
    assert nodes == [0, 1, 2]


def test_pearce_kelly_2007_example():
    # Example based on figure in https://doi.org/10.1145/1187436.1210590
    # Existing edges: y -> a -> c; b -> x
    # New edge: x -> y
    # Before: _ y _ _ a _ b _ _ c _  x  _
    # After:  _ b _ _ x _ y _ _ a _  c  _
    #         0 1 2 3 4 5 6 7 8 9 10 11 12
    nodes = list(range(13))
    y, a, b, c, x = 1, 4, 6, 9, 11  # Others not involved.
    edge = _Edge(x, y, ())
    edge_ya = _Edge(y, a, ())
    edge_ac = _Edge(a, c, ())
    edge_bx = _Edge(b, x, ())
    input_edges = {i: (None,) for i in range(13)}
    input_edges[a] = (edge_ya,)
    input_edges[c] = (edge_ac,)
    input_edges[x] = (edge_bx,)
    output_edges = {i: (None,) for i in range(13)}
    output_edges[y] = (edge_ya,)
    output_edges[a] = (edge_ac,)
    output_edges[b] = (edge_bx,)
    _update_topological_order(nodes, input_edges, output_edges, edge)
    assert nodes == [0, b, 2, 3, x, 5, y, 7, 8, a, 10, c, 12]
