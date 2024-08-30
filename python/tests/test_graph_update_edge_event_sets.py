# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._cpp_utils import CppTypeName
from libtcspc._events import EventType
from libtcspc._graph import Node, _Edge, _update_edge_event_sets

ShortEvent = EventType(CppTypeName("short"))
IntEvent = EventType(CppTypeName("int"))
LongEvent = EventType(CppTypeName("long"))


def test_empty():
    _update_edge_event_sets([], [], {}, {})


def test_single_node(mocker):
    node = Node(input=["i0", "i1"], output=["o0", "o1"])
    node.map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((IntEvent), ())
    )
    _update_edge_event_sets(
        [("node", node)],
        [0],
        {0: [None, None]},
        {0: [None, None]},
    )
    node.map_event_sets.assert_called_with([(), ()])  # type: ignore


def test_single_node_with_pseudo_edges(mocker):
    node = Node(input=["i0", "i1"], output=["o0", "o1"])
    node.map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((IntEvent,), ())
    )
    output0_pseudo_edge = _Edge(0, -1, ())
    _update_edge_event_sets(
        [("node", node)],
        [0],
        {0: [_Edge(-1, 0, (LongEvent,)), None]},
        {0: [output0_pseudo_edge, None]},
    )
    node.map_event_sets.assert_called_with([(LongEvent,), ()])  # type: ignore
    assert output0_pseudo_edge.event_set == (IntEvent,)


def test_two_nodes(mocker):
    node0 = Node()
    node1 = Node()
    edge01 = _Edge(0, 1, ())
    node0.map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((ShortEvent,),)
    )
    node1.map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((LongEvent,),)
    )
    _update_edge_event_sets(
        [("node0", node0), ("node1", node1)],
        [0, 1],
        {0: [None], 1: [edge01]},
        {0: [edge01], 1: [None]},
    )
    node0.map_event_sets.assert_called_with([()])  # type: ignore
    assert edge01.event_set == (ShortEvent,)
    node1.map_event_sets.assert_called_with([edge01.event_set])  # type: ignore
