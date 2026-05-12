# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent, _TestNode
from libtcspc._cpp_utils import _CppTypeName
from libtcspc._graph import _Edge, _update_edge_event_sets

ShortEvent = _NamedEvent(_CppTypeName("short"))
IntEvent = _NamedEvent(_CppTypeName("int"))
LongEvent = _NamedEvent(_CppTypeName("long"))


def test_empty():
    _update_edge_event_sets([], [], {}, {})


def test_single_node(mocker):
    node = _TestNode(input=["i0", "i1"], output=["o0", "o1"])
    node._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((IntEvent), ())
    )
    _update_edge_event_sets(
        [("node", node)],
        [0],
        {0: [None, None]},
        {0: [None, None]},
    )
    node._map_event_sets.assert_called_with([(), ()])  # type: ignore


def test_single_node_with_pseudo_edges(mocker):
    node = _TestNode(input=["i0", "i1"], output=["o0", "o1"])
    node._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((IntEvent,), ())
    )
    output0_pseudo_edge = _Edge(0, -1, ())
    _update_edge_event_sets(
        [("node", node)],
        [0],
        {0: [_Edge(-1, 0, (LongEvent,)), None]},
        {0: [output0_pseudo_edge, None]},
    )
    node._map_event_sets.assert_called_with([(LongEvent,), ()])  # type: ignore
    assert output0_pseudo_edge.event_set == (IntEvent,)


def test_two_nodes(mocker):
    node0 = _TestNode()
    node1 = _TestNode()
    edge01 = _Edge(0, 1, ())
    node0._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((ShortEvent,),)
    )
    node1._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((LongEvent,),)
    )
    _update_edge_event_sets(
        [("node0", node0), ("node1", node1)],
        [0, 1],
        {0: [None], 1: [edge01]},
        {0: [edge01], 1: [None]},
    )
    node0._map_event_sets.assert_called_with([()])  # type: ignore
    assert edge01.event_set == (ShortEvent,)
    node1._map_event_sets.assert_called_with([edge01.event_set])  # type: ignore
