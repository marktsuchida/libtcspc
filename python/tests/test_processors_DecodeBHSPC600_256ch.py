# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import (
    BHSPC600_256chEvent,
    DataLostEvent,
    TimeCorrelatedDetectionEvent,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._processors import DecodeBHSPC600_256ch

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodeBHSPC600_256ch_accepts_only_BHSPC600_256chEvent():
    node = DecodeBHSPC600_256ch()
    out = node._map_event_sets([(BHSPC600_256chEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeBHSPC600_256ch_output_event_set():
    dt = NumericTraits()
    node = DecodeBHSPC600_256ch(dt)
    assert node._map_event_sets([(BHSPC600_256chEvent(),)]) == (
        (
            DataLostEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeBHSPC600_256ch_codegen():
    node = DecodeBHSPC600_256ch()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_bh_spc600_256ch<" in code
    assert "DOWN" in code
