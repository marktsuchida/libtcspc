# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import (
    DetectionEvent,
    MarkerEvent,
    PQT2HydraHarpV1Event,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._processors import DecodePQT2HydraHarpV1

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodePQT2HydraHarpV1_accepts_only_matching_input():
    node = DecodePQT2HydraHarpV1()
    out = node._map_event_sets([(PQT2HydraHarpV1Event(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT2HydraHarpV1_output_event_set():
    dt = NumericTraits()
    node = DecodePQT2HydraHarpV1(dt)
    assert node._map_event_sets([(PQT2HydraHarpV1Event(),)]) == (
        (
            DetectionEvent(dt),
            MarkerEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT2HydraHarpV1_codegen():
    node = DecodePQT2HydraHarpV1()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt2_hydraharpv1<" in code
    assert "DOWN" in code
