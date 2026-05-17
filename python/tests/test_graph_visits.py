# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence

import pytest
from _test_helpers import _NamedEvent, _TestRelayNode
from libtcspc._access import AccessTag
from libtcspc._cpp_utils import _CppTypeName
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import Count, SinkAll

IntEvent = _NamedEvent(_CppTypeName("int"))


class _ParamNode(_TestRelayNode):
    def __init__(self, param_name: str, default: object = None) -> None:
        self._param_name = param_name
        self._default = default

    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return ((Param(self._param_name, self._default), _CppTypeName("int")),)


def test_graph_visits_collects_no_parameters_for_paramless_graph():
    g = Graph()
    assert g._parameters() == []

    g.add_node("a", _TestRelayNode())
    g.add_node("b", SinkAll())
    assert g._parameters() == []


def test_graph_visits_collects_parameters_in_visit_order():
    g = Graph()
    g.add_node("a", _ParamNode("p1"))
    g.add_node("b", _ParamNode("p2"))
    g.add_node("c", _ParamNode("p3"))
    params = g._parameters()
    assert {p.name for p, _ in params} == {"p1", "p2", "p3"}


def test_graph_visits_duplicate_parameters():
    g = Graph()
    g.add_node("a", _ParamNode("hello"))
    g.add_node("b", _ParamNode("world"))
    assert len(g._parameters()) == 2
    g.add_node("c", _ParamNode("hello"))
    with pytest.raises(ValueError, match="param.*hello"):
        g._parameters()


def test_graph_visits_duplicate_parameter_error_lists_all_nodes():
    g = Graph()
    g.add_node("a", _ParamNode("dup"))
    g.add_node("b", _ParamNode("dup"))
    g.add_node("c", _ParamNode("dup"))
    with pytest.raises(ValueError) as exc_info:
        g._parameters()
    msg = str(exc_info.value)
    assert "a" in msg
    assert "b" in msg
    assert "c" in msg


def test_graph_visits_duplicate_parameter_with_different_defaults():
    # Duplicate detection keys on Param.name only, not full equality —
    # different defaults still raise.
    g = Graph()
    g.add_node("a", _ParamNode("p", 1))
    g.add_node("b", _ParamNode("p", 2))
    with pytest.raises(ValueError, match="param.*p"):
        g._parameters()


def test_graph_visits_duplicate_access_tags():
    g = Graph()
    g.add_node("a", Count(IntEvent, AccessTag("hello")))
    g.add_node("b", Count(IntEvent, AccessTag("world")))
    assert len(g._accesses()) == 2
    g.add_node("c", Count(IntEvent, AccessTag("hello")))
    with pytest.raises(ValueError, match="tag.*hello"):
        g._accesses()
