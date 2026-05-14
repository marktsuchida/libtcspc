# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _int64_type,
    _size_type,
)
from libtcspc._param import Param
from libtcspc._processors import RegulateTimeReached

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_RegulateTimeReached_event_set_is_preserved():
    node = RegulateTimeReached(100, 1000)
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_RegulateTimeReached_codegen():
    node = RegulateTimeReached(100, 1000)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::regulate_time_reached<" in code
    assert "tcspc::i64{100LL}" in code
    assert "std::size_t{1000uLL}" in code
    assert "DOWN" in code


def test_RegulateTimeReached_params_wire():
    node = RegulateTimeReached(Param("it"), Param("ct"))
    params = node._parameters()
    assert (Param("it"), _int64_type) in params
    assert (Param("ct"), _size_type) in params
