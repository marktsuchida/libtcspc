# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._processors import SelectExcept

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))
ThirdEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


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
