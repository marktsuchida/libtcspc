# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_AddCountToPeriodicSequences_event_set():
    node = tcspc.AddCountToPeriodicSequences(10)
    (out,) = node._map_event_sets([(tcspc.PeriodicSequenceModelEvent(),)])
    assert out == (tcspc.RealLinearTimingEvent(),)


def test_AddCountToPeriodicSequences_codegen():
    code = tcspc.AddCountToPeriodicSequences(10)._cpp_expression(
        gencontext, DOWN
    )
    assert (
        "tcspc::add_count_to_periodic_sequences<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::count<std::size_t>" in code
    assert "DOWN" in code
