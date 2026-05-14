# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._data_types import DataTypes
from libtcspc._events import (
    DetectionEvent,
    MarkerEvent,
    PQT2GenericEvent,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._processors import DecodePQT2Generic

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodePQT2Generic_accepts_only_matching_input():
    node = DecodePQT2Generic()
    out = node._map_event_sets([(PQT2GenericEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT2Generic_output_event_set():
    dt = DataTypes()
    node = DecodePQT2Generic(dt)
    assert node._map_event_sets([(PQT2GenericEvent(),)]) == (
        (
            DetectionEvent(dt),
            MarkerEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT2Generic_codegen():
    node = DecodePQT2Generic()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt2_generic<" in code
    assert "DOWN" in code
