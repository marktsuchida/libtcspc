# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._data_types import DataTypes
from libtcspc._events import (
    BeginLostIntervalEvent,
    DetectionEvent,
    EndLostIntervalEvent,
    LostCountsEvent,
    SwabianTagEvent,
    WarningEvent,
)
from libtcspc._processors import DecodeSwabianTags

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodeSwabianTags_accepts_only_matching_input():
    node = DecodeSwabianTags()
    out = node._map_event_sets([(SwabianTagEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeSwabianTags_output_event_set():
    dt = DataTypes()
    node = DecodeSwabianTags(dt)
    assert node._map_event_sets([(SwabianTagEvent(),)]) == (
        (
            BeginLostIntervalEvent(dt),
            DetectionEvent(dt),
            EndLostIntervalEvent(dt),
            LostCountsEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeSwabianTags_codegen():
    node = DecodeSwabianTags()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_swabian_tags<" in code
    assert "DOWN" in code
