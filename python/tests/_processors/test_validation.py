# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import WarningEvent
from libtcspc._numeric_traits import NumericTraits
from libtcspc._processors import CheckAlternating, CheckMonotonic

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))
ThirdEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_CheckAlternating_adds_WarningEvent_to_output():
    node = CheckAlternating(IntEvent, OtherEvent)
    assert node._map_event_sets([(IntEvent, OtherEvent, ThirdEvent)]) == (
        (IntEvent, OtherEvent, ThirdEvent, WarningEvent()),
    )


def test_CheckAlternating_does_not_duplicate_WarningEvent_in_output():
    node = CheckAlternating(IntEvent, OtherEvent)
    assert node._map_event_sets([(IntEvent, OtherEvent, WarningEvent())]) == (
        (IntEvent, OtherEvent, WarningEvent()),
    )


def test_CheckAlternating_codegen_calls_tcspc_check_alternating():
    node = CheckAlternating(IntEvent, OtherEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::check_alternating<" in code
    assert "int" in code
    assert "long" in code
    assert "DOWN" in code


def test_CheckMonotonic_adds_WarningEvent_to_output():
    node = CheckMonotonic()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent, WarningEvent()),
    )


def test_CheckMonotonic_does_not_duplicate_WarningEvent_in_output():
    node = CheckMonotonic()
    assert node._map_event_sets([(IntEvent, OtherEvent, WarningEvent())]) == (
        (IntEvent, OtherEvent, WarningEvent()),
    )


def test_CheckMonotonic_codegen_default_numeric_traits():
    node = CheckMonotonic()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::check_monotonic<" in code
    assert NumericTraits()._cpp_type_name() in code
    assert "DOWN" in code


def test_CheckMonotonic_codegen_explicit_numeric_traits():
    dt = NumericTraits(abstime_type=np.uint32)
    node = CheckMonotonic(dt)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert dt._cpp_type_name() in code
