# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_SelectNone_emits_nothing():
    assert tcspc.SelectNone()._map_event_sets([(IntEvent,)]) == ((),)


def test_SelectNone_codegen():
    code = tcspc.SelectNone()._cpp_expression(gencontext, DOWN)
    assert code == "tcspc::select_none(DOWN)"
