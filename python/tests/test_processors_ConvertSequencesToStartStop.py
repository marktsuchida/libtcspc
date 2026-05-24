# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

TickEvent = _NamedEvent(_CppTypeName("int"))
StartEvent = _NamedEvent(_CppTypeName("long"))
StopEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_ConvertSequencesToStartStop_event_set():
    node = tcspc.ConvertSequencesToStartStop(
        TickEvent, StartEvent, StopEvent, 3
    )
    (out,) = node._map_event_sets([(TickEvent,)])
    cpp = [e._cpp_type_name() for e in out]
    assert TickEvent._cpp_type_name() not in cpp
    assert StartEvent._cpp_type_name() in cpp
    assert StopEvent._cpp_type_name() in cpp


def test_ConvertSequencesToStartStop_codegen():
    node = tcspc.ConvertSequencesToStartStop(
        TickEvent, StartEvent, StopEvent, 3
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::convert_sequences_to_start_stop<" in code
    assert "int" in code and "long" in code and "short" in code
    assert "tcspc::arg::count<std::size_t>" in code
    assert "DOWN" in code
