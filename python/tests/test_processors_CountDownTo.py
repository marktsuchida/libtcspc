# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _uint64_type,
)
from libtcspc._param import Param
from libtcspc._processors import CountDownTo

TickEvent = _NamedEvent(_CppTypeName("int"))
FireEvent = _NamedEvent(_CppTypeName("long"))
ResetEvent = _NamedEvent(_CppTypeName("short"))
OtherEvent = _NamedEvent(_CppTypeName("unsigned"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_CountDownTo_output_includes_fire_event():
    node = CountDownTo(TickEvent, FireEvent, ResetEvent, 5, 0, 10)
    out = node._map_event_sets([(TickEvent, OtherEvent)])
    assert FireEvent in out[0]


def test_CountDownTo_codegen_calls_tcspc_count_down_to():
    node = CountDownTo(TickEvent, FireEvent, ResetEvent, 5, 0, 10)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::count_down_to<" in code
    assert "false" in code
    assert "DOWN" in code


def test_CountDownTo_params_wire():
    node = CountDownTo(
        TickEvent,
        FireEvent,
        ResetEvent,
        Param("t"),
        Param("l"),
        Param("i"),
    )
    params = node._parameters()
    assert (Param("t"), _uint64_type) in params
    assert (Param("l"), _uint64_type) in params
    assert (Param("i"), _uint64_type) in params
