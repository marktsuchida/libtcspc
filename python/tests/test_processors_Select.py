# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._processors import Select

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))
ThirdEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


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
