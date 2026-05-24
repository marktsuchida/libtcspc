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


def test_PairOne_event_set_adds_pair():
    node = tcspc.PairOne(0, [1, 2], 1000)
    (out,) = node._map_event_sets([(tcspc.DetectionEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.DetectionPairEvent()._cpp_type_name() in cpp


def test_PairOne_codegen():
    code = tcspc.PairOne(0, [1, 2], 1000)._cpp_expression(gencontext, DOWN)
    assert "tcspc::pair_one<2, tcspc::default_numeric_traits>(" in code
    assert "DOWN" in code
