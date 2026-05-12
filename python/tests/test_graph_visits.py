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
from libtcspc._processors import Count

IntEvent = _NamedEvent(_CppTypeName("int"))


def test_graph_visits_duplicate_parameters():
    class ParamNode(_TestRelayNode):
        def __init__(self, param_name: str) -> None:
            self._param_name = param_name

        def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
            return ((Param(self._param_name), _CppTypeName("int")),)

    g = Graph()
    g.add_node("a", ParamNode("hello"))
    g.add_node("b", ParamNode("world"))
    assert len(g._parameters()) == 2
    g.add_node("c", ParamNode("hello"))
    with pytest.raises(ValueError, match="param.*hello"):
        g._parameters()


def test_graph_visits_duplicate_access_tags():
    g = Graph()
    g.add_node("a", Count(IntEvent, AccessTag("hello")))
    g.add_node("b", Count(IntEvent, AccessTag("world")))
    assert len(g._accesses()) == 2
    g.add_node("c", Count(IntEvent, AccessTag("hello")))
    with pytest.raises(ValueError, match="tag.*hello"):
        g._accesses()
