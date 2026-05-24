# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._param import Param
from libtcspc._processors import Select, SelectAll, SelectExcept

GatedEvent = _NamedEvent(_CppTypeName("int"))
OpenEvent = _NamedEvent(_CppTypeName("long"))
CloseEvent = _NamedEvent(_CppTypeName("short"))

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))
ThirdEvent = _NamedEvent(_CppTypeName("short"))

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


def test_Select_keeps_listed_events_only():
    node = Select(IntEvent)
    assert node._map_event_sets([(IntEvent, OtherEvent, ThirdEvent)]) == (
        (IntEvent,),
    )


def test_Select_keeps_multiple_listed_events():
    node = Select(IntEvent, OtherEvent)
    out = node._map_event_sets([(IntEvent, OtherEvent, ThirdEvent)])[0]
    assert IntEvent in out
    assert OtherEvent in out
    assert ThirdEvent not in out


def test_Select_empty_input_yields_empty_output():
    node = Select(IntEvent)
    assert node._map_event_sets([()]) == ((),)


def test_Select_codegen_calls_tcspc_select():
    node = Select(IntEvent, OtherEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::select<" in code
    assert "tcspc::type_list<int, long>" in code
    assert "DOWN" in code


def test_SelectAll_event_set_is_preserved():
    node = SelectAll()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_SelectAll_codegen_is_tcspc_select_all():
    node = SelectAll()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::select_all(DOWN)" in code


def test_SelectExcept_drops_listed_events():
    node = SelectExcept(IntEvent)
    assert node._map_event_sets([(IntEvent, OtherEvent, ThirdEvent)]) == (
        (OtherEvent, ThirdEvent),
    )


def test_SelectExcept_drops_multiple_listed_events():
    node = SelectExcept(IntEvent, OtherEvent)
    out = node._map_event_sets([(IntEvent, OtherEvent, ThirdEvent)])[0]
    assert IntEvent not in out
    assert OtherEvent not in out
    assert ThirdEvent in out


def test_SelectExcept_codegen_calls_tcspc_select_except():
    node = SelectExcept(IntEvent, OtherEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::select_except<" in code
    assert "tcspc::type_list<int, long>" in code
    assert "DOWN" in code


def test_SelectNone_emits_nothing():
    assert tcspc.SelectNone()._map_event_sets([(IntEvent,)]) == ((),)


def test_SelectNone_codegen():
    code = tcspc.SelectNone()._cpp_expression(gencontext, DOWN)
    assert code == "tcspc::select_none(DOWN)"
