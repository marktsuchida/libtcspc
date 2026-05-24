# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_TimeCorrelateAtMidpoint_event_set():
    node = tcspc.TimeCorrelateAtMidpoint()
    (out,) = node._map_event_sets([(tcspc.DetectionPairEvent(),)])
    assert out == (tcspc.TimeCorrelatedDetectionEvent(),)


def test_TimeCorrelateAtMidpoint_codegen_default():
    code = tcspc.TimeCorrelateAtMidpoint()._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::time_correlate_at_midpoint<tcspc::default_numeric_traits, false>("
        in code
    )


def test_TimeCorrelateAtMidpoint_codegen_use_start_channel():
    code = tcspc.TimeCorrelateAtMidpoint(
        use_start_channel=True
    )._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::time_correlate_at_midpoint<tcspc::default_numeric_traits, true>("
        in code
    )
