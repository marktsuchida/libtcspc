# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._data_types import DataTypes
from libtcspc._events import (
    MarkerEvent,
    PQT3GenericEvent,
    TimeCorrelatedDetectionEvent,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._processors import DecodePQT3Generic

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodePQT3Generic_accepts_only_matching_input():
    node = DecodePQT3Generic()
    out = node._map_event_sets([(PQT3GenericEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT3Generic_output_event_set():
    dt = DataTypes()
    node = DecodePQT3Generic(dt)
    assert node._map_event_sets([(PQT3GenericEvent(),)]) == (
        (
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT3Generic_codegen():
    node = DecodePQT3Generic()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt3_generic<" in code
    assert "DOWN" in code
