# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

TriggerEvent = _NamedEvent(_CppTypeName("int"))
OutputEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_Generate_event_set_adds_output():
    node = tcspc.Generate(
        TriggerEvent, OutputEvent, tcspc.NullTimingGenerator()
    )
    (out,) = node._map_event_sets([(TriggerEvent,)])
    cpp = [e._cpp_type_name() for e in out]
    assert TriggerEvent._cpp_type_name() in cpp
    assert OutputEvent._cpp_type_name() in cpp


def test_Generate_codegen():
    node = tcspc.Generate(
        TriggerEvent, OutputEvent, tcspc.OneShotTimingGenerator(50)
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::generate<\n                int,\n                long\n"
        in code
    )
    assert "tcspc::one_shot_timing_generator<" in code
    assert "DOWN" in code
