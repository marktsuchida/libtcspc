# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._data_types import DataTypes
from libtcspc._events import (
    BHSPCEvent,
    DataLostEvent,
    MarkerEvent,
    TimeCorrelatedDetectionEvent,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._processors import DecodeBHSPC

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodeBHSPC_accepts_only_BHSPCEvent():
    node = DecodeBHSPC()
    out = node._map_event_sets([(BHSPCEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeBHSPC_output_event_set():
    dt = DataTypes()
    node = DecodeBHSPC(dt)
    assert node._map_event_sets([(BHSPCEvent(),)]) == (
        (
            DataLostEvent(dt),
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeBHSPC_codegen_calls_decode_bh_spc():
    dt = DataTypes()
    node = DecodeBHSPC(dt)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_bh_spc<" in code
    assert dt._cpp_type_name() in code
    assert "DOWN" in code
