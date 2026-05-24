# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

InEvent = _NamedEvent(_CppTypeName("int"))
OutEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_MatchAndConsume_event_set_adds_out_event():
    node = tcspc.MatchAndConsume(InEvent, OutEvent, tcspc.AlwaysMatcher())
    (out,) = node._map_event_sets([(InEvent,)])
    cpp = [e._cpp_type_name() for e in out]
    assert OutEvent._cpp_type_name() in cpp


def test_MatchAndConsume_codegen():
    node = tcspc.MatchAndConsume(InEvent, OutEvent, tcspc.AlwaysMatcher())
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::match_and_consume<" in code
    assert "tcspc::always_matcher()" in code
    assert "DOWN" in code
