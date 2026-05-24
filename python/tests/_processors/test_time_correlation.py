# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _int64_type,
)
from libtcspc._events import (
    DetectionEvent,
    TimeCorrelatedDetectionEvent,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._param import Param
from libtcspc._processors import RecoverOrder, RemoveTimeCorrelation

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))
RemoveTC_OtherEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_RecoverOrder_event_set_is_preserved():
    node = RecoverOrder(100)
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_RecoverOrder_codegen_int():
    node = RecoverOrder(100)
    node._map_event_sets([(IntEvent, OtherEvent)])
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::recover_order<" in code
    assert "tcspc::type_list<int, long>" in code
    assert "tcspc::i64{100LL}" in code
    assert "DOWN" in code


def test_RecoverOrder_codegen_param():
    node = RecoverOrder(Param("tw"))
    node._map_event_sets([(IntEvent,)])
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("tw"), _int64_type)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "params.z_tw" in code


def test_RemoveTimeCorrelation_replaces_tcd_with_detection():
    dt = NumericTraits()
    node = RemoveTimeCorrelation(dt)
    out = node._map_event_sets(
        [(TimeCorrelatedDetectionEvent(dt), RemoveTC_OtherEvent)]
    )
    assert RemoveTC_OtherEvent in out[0]
    assert DetectionEvent(dt) in out[0]
    assert TimeCorrelatedDetectionEvent(dt) not in out[0]


def test_RemoveTimeCorrelation_appends_detection_if_not_present():
    dt = NumericTraits()
    node = RemoveTimeCorrelation(dt)
    out = node._map_event_sets([(RemoveTC_OtherEvent,)])
    assert RemoveTC_OtherEvent in out[0]
    assert DetectionEvent(dt) in out[0]


def test_RemoveTimeCorrelation_codegen():
    node = RemoveTimeCorrelation()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::remove_time_correlation<" in code
    assert NumericTraits()._cpp_type_name() in code
    assert "DOWN" in code


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


def test_TimeCorrelateAtStop_event_set():
    node = tcspc.TimeCorrelateAtStop()
    (out,) = node._map_event_sets([(tcspc.DetectionPairEvent(),)])
    assert out == (tcspc.TimeCorrelatedDetectionEvent(),)


def test_TimeCorrelateAtStop_codegen():
    code = tcspc.TimeCorrelateAtStop()._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::time_correlate_at_stop<tcspc::default_numeric_traits>(" in code
    )
    assert "DOWN" in code
