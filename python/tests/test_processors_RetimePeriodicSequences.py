# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_RetimePeriodicSequences_event_set():
    node = tcspc.RetimePeriodicSequences(100)
    (out,) = node._map_event_sets([(tcspc.PeriodicSequenceModelEvent(),)])
    assert out == (tcspc.PeriodicSequenceModelEvent(),)


def test_RetimePeriodicSequences_rejects_other_events():
    node = tcspc.RetimePeriodicSequences(100)
    with pytest.raises(ValueError):
        node._map_event_sets(
            [
                (
                    tcspc.PeriodicSequenceModelEvent(),
                    _NamedEvent(_CppTypeName("int")),
                )
            ]
        )


def test_RetimePeriodicSequences_codegen():
    code = tcspc.RetimePeriodicSequences(100)._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::retime_periodic_sequences<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::max_time_shift<" in code
    assert "DOWN" in code
