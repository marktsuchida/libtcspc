# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._param import Param

TickEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_FitPeriodicSequences_event_set():
    node = tcspc.FitPeriodicSequences(TickEvent, 5, 1.0, 2.0, 0.1)
    (out,) = node._map_event_sets([(TickEvent,)])
    assert out == (tcspc.PeriodicSequenceModelEvent(),)


def test_FitPeriodicSequences_codegen():
    node = tcspc.FitPeriodicSequences(TickEvent, 5, 1.0, 2.0, 0.1)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::fit_periodic_sequences<\n                int," in code
    assert "tcspc::arg::length<std::size_t>" in code
    assert "tcspc::arg::min_interval<double>{1.0}" in code
    assert "tcspc::arg::max_interval<double>{2.0}" in code
    assert "tcspc::arg::max_mse<double>{0.1}" in code
    assert "DOWN" in code


def test_FitPeriodicSequences_params():
    node = tcspc.FitPeriodicSequences(
        TickEvent, Param("n"), Param("mn"), Param("mx"), Param("e")
    )
    assert len(node._parameters()) == 4
