# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._processors import NullSource

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_NullSource_rejects_nonempty_input_set():
    node = NullSource()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_NullSource_output_event_set_is_empty():
    node = NullSource()
    assert node._map_event_sets([()]) == ((),)


def test_NullSource_codegen_calls_tcspc_null_source():
    node = NullSource()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::null_source(" in code
    assert "DOWN" in code
