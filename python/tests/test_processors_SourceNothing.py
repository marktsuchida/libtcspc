# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._processors import SourceNothing

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_SourceNothing_rejects_nonempty_input_set():
    node = SourceNothing()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_SourceNothing_output_event_set_is_empty():
    node = SourceNothing()
    assert node._map_event_sets([()]) == ((),)


def test_SourceNothing_codegen_calls_tcspc_source_nothing():
    node = SourceNothing()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::source_nothing(" in code
    assert "DOWN" in code
