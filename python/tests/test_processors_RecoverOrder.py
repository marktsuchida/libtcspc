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
)
from libtcspc._param import Param
from libtcspc._processors import RecoverOrder

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_RecoverOrder_event_set_is_preserved():
    node = RecoverOrder(100)
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_RecoverOrder_codegen_int():
    node = RecoverOrder(100)
    node._map_event_sets([(IntEvent, OtherEvent)])
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::recover_order<" in code
    assert "tcspc::type_list<int, long>" in code
    assert "tcspc::i64{100LL}" in code
    assert "DOWN" in code


def test_RecoverOrder_codegen_param():
    node = RecoverOrder(Param("tw"))
    node._map_event_sets([(IntEvent,)])
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("tw"), _int64_type)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "params.z_tw" in code
