# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier
from libtcspc._param import Param

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_PairAll_event_set_adds_pair():
    node = tcspc.PairAll(0, [1, 2], 1000)
    (out,) = node._map_event_sets([(tcspc.DetectionEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.DetectionEvent()._cpp_type_name() in cpp
    assert tcspc.DetectionPairEvent()._cpp_type_name() in cpp


def test_PairAll_codegen():
    node = tcspc.PairAll(0, [1, 2], 1000)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::pair_all<2, tcspc::default_numeric_traits>(" in code
    assert "tcspc::arg::start_channel<" in code
    assert "std::array<" in code
    assert "tcspc::arg::time_window<" in code
    assert "DOWN" in code


def test_PairAll_params():
    node = tcspc.PairAll(Param("sc"), [1], Param("tw"))
    params = node._parameters()
    assert len(params) == 2
    code = node._cpp_expression(gencontext, DOWN)
    assert "params.z_sc" in code
    assert "params.z_tw" in code
