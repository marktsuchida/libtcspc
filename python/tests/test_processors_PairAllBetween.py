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


def test_PairAllBetween_event_set_adds_pair():
    node = tcspc.PairAllBetween(0, [1], 1000)
    (out,) = node._map_event_sets([(tcspc.DetectionEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.DetectionPairEvent()._cpp_type_name() in cpp


def test_PairAllBetween_codegen():
    code = tcspc.PairAllBetween(0, [1], 1000)._cpp_expression(gencontext, DOWN)
    assert "tcspc::pair_all_between<1, tcspc::default_numeric_traits>(" in code
    assert "DOWN" in code
