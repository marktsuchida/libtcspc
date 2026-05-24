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


def test_MapToDatapoints_event_set():
    node = tcspc.MapToDatapoints(
        tcspc.TimeCorrelatedDetectionEvent(), tcspc.DifftimeDataMapper()
    )
    (out,) = node._map_event_sets([(tcspc.TimeCorrelatedDetectionEvent(),)])
    assert out == (tcspc.DatapointEvent(),)


def test_MapToDatapoints_codegen():
    node = tcspc.MapToDatapoints(
        tcspc.TimeCorrelatedDetectionEvent(), tcspc.DifftimeDataMapper()
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::map_to_datapoints<" in code
    assert "tcspc::time_correlated_detection_event<" in code
    assert "tcspc::difftime_data_mapper<" in code
    assert "DOWN" in code
