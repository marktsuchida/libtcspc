# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_TimeCorrelateAtStart_event_set():
    node = tcspc.TimeCorrelateAtStart()
    (out,) = node._map_event_sets([(tcspc.DetectionPairEvent(), OtherEvent)])
    cpp = [e._cpp_type_name() for e in out]
    assert OtherEvent._cpp_type_name() in cpp
    assert tcspc.TimeCorrelatedDetectionEvent()._cpp_type_name() in cpp
    assert tcspc.DetectionPairEvent()._cpp_type_name() not in cpp


def test_TimeCorrelateAtStart_codegen():
    code = tcspc.TimeCorrelateAtStart()._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::time_correlate_at_start<tcspc::default_numeric_traits>("
        in code
    )
    assert "DOWN" in code
