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


def test_ExtrapolatePeriodicSequences_event_set():
    node = tcspc.ExtrapolatePeriodicSequences(2)
    (out,) = node._map_event_sets([(tcspc.PeriodicSequenceModelEvent(),)])
    assert out == (tcspc.RealOneShotTimingEvent(),)


def test_ExtrapolatePeriodicSequences_codegen():
    code = tcspc.ExtrapolatePeriodicSequences(2)._cpp_expression(
        gencontext, DOWN
    )
    assert (
        "tcspc::extrapolate_periodic_sequences<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::tick_index<std::size_t>" in code
    assert "DOWN" in code
