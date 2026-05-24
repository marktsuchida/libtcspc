# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._param import Param

GatedEvent = _NamedEvent(_CppTypeName("int"))
OpenEvent = _NamedEvent(_CppTypeName("long"))
CloseEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def _node(initially_open=False):
    return tcspc.Gate(
        GatedEvent,
        open_event_type=OpenEvent,
        close_event_type=CloseEvent,
        initially_open=initially_open,
    )


def test_Gate_event_set_preserved():
    inputs = (GatedEvent, OpenEvent, CloseEvent)
    assert _node()._map_event_sets([inputs]) == (inputs,)


def test_Gate_codegen():
    code = _node(initially_open=True)._cpp_expression(gencontext, DOWN)
    assert "tcspc::gate<" in code
    assert "tcspc::type_list<int>" in code
    assert "long" in code and "short" in code
    assert "tcspc::arg::initially_open{static_cast<bool>(true)}" in code
    assert "DOWN" in code


def test_Gate_initially_open_false():
    code = _node(initially_open=False)._cpp_expression(gencontext, DOWN)
    assert "static_cast<bool>(false)" in code


def test_Gate_param():
    node = tcspc.Gate(
        GatedEvent,
        open_event_type=OpenEvent,
        close_event_type=CloseEvent,
        initially_open=Param("o"),
    )
    assert len(node._parameters()) == 1
    assert "params.z_o" in node._cpp_expression(gencontext, DOWN)
