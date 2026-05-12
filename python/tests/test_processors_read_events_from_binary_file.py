# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._cpp_utils import _CppTypeName
from libtcspc._graph import Subgraph
from libtcspc._node import Node
from libtcspc._param import Param
from libtcspc._processors import (
    Stop,
    StopWithError,
    read_events_from_binary_file,
)

IntEvent = _NamedEvent(_CppTypeName("int"))


def _subgraph(**kwargs) -> Subgraph:
    sg = read_events_from_binary_file(IntEvent, "f.bin", **kwargs)
    assert isinstance(sg, Subgraph)
    return sg


def test_returns_subgraph_with_input_and_output_ports():
    sg = _subgraph()
    assert sg.inputs() == ("input",)
    assert sg.outputs() == ("output",)


def test_subgraph_output_emits_element_event_type():
    sg = _subgraph()
    assert sg._map_event_sets([()]) == ((IntEvent,),)


def test_stop_normally_on_error_True_uses_Stop():
    sg = _subgraph(stop_normally_on_error=True)
    found: dict[str, bool] = {"stop": False, "stop_with_error": False}

    def visit(_: str, node: Node) -> None:
        if isinstance(node, Stop):
            found["stop"] = True
        elif isinstance(node, StopWithError):
            found["stop_with_error"] = True

    sg.graph().visit_nodes(visit)
    assert found["stop"]
    assert not found["stop_with_error"]


def test_stop_normally_on_error_False_uses_StopWithError():
    sg = _subgraph()  # default: stop_normally_on_error=False
    found: dict[str, bool] = {"stop": False, "stop_with_error": False}

    def visit(_: str, node: Node) -> None:
        if isinstance(node, Stop):
            found["stop"] = True
        elif isinstance(node, StopWithError):
            found["stop_with_error"] = True

    sg.graph().visit_nodes(visit)
    assert found["stop_with_error"]
    assert not found["stop"]


def test_params_propagate_through_to_parameters():
    sg = read_events_from_binary_file(
        IntEvent, Param("f"), max_length=Param("m")
    )
    names = [p.name for p, _ in sg._parameters()]
    assert "f" in names
    assert "m" in names
