# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier
from libtcspc._param import Param

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_TimeCorrelateAtFraction_event_set():
    node = tcspc.TimeCorrelateAtFraction(0.5)
    (out,) = node._map_event_sets([(tcspc.DetectionPairEvent(),)])
    assert out == (tcspc.TimeCorrelatedDetectionEvent(),)


def test_TimeCorrelateAtFraction_codegen():
    code = tcspc.TimeCorrelateAtFraction(
        0.25, use_start_channel=True
    )._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::time_correlate_at_fraction<tcspc::default_numeric_traits, true>("
        in code
    )
    assert "tcspc::arg::fraction<double>{0.25}" in code


def test_TimeCorrelateAtFraction_param():
    node = tcspc.TimeCorrelateAtFraction(Param("f"))
    assert len(node._parameters()) == 1
    assert "params.z_f" in node._cpp_expression(gencontext, DOWN)
