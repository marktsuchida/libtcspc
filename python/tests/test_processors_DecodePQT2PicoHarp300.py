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
    PQT2PicoHarp300Event,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._processors import DecodePQT2PicoHarp300

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodePQT2PicoHarp300_accepts_only_matching_input():
    node = DecodePQT2PicoHarp300()
    out = node._map_event_sets([(PQT2PicoHarp300Event(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT2PicoHarp300_output_event_set():
    dt = DataTypes()
    node = DecodePQT2PicoHarp300(dt)
    assert node._map_event_sets([(PQT2PicoHarp300Event(),)]) == (
        (
            DetectionEvent(dt),
            MarkerEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT2PicoHarp300_codegen():
    node = DecodePQT2PicoHarp300()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt2_picoharp300<" in code
    assert "DOWN" in code
