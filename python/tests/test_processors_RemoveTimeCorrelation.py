# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import (
    DetectionEvent,
    TimeCorrelatedDetectionEvent,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._processors import RemoveTimeCorrelation

OtherEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_RemoveTimeCorrelation_replaces_tcd_with_detection():
    dt = NumericTraits()
    node = RemoveTimeCorrelation(dt)
    out = node._map_event_sets(
        [(TimeCorrelatedDetectionEvent(dt), OtherEvent)]
    )
    assert OtherEvent in out[0]
    assert DetectionEvent(dt) in out[0]
    assert TimeCorrelatedDetectionEvent(dt) not in out[0]


def test_RemoveTimeCorrelation_appends_detection_if_not_present():
    dt = NumericTraits()
    node = RemoveTimeCorrelation(dt)
    out = node._map_event_sets([(OtherEvent,)])
    assert OtherEvent in out[0]
    assert DetectionEvent(dt) in out[0]


def test_RemoveTimeCorrelation_codegen():
    node = RemoveTimeCorrelation()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::remove_time_correlation<" in code
    assert NumericTraits()._cpp_type_name() in code
    assert "DOWN" in code
