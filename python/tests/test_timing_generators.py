# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier
from libtcspc._param import Param
from libtcspc._timing_generators import (
    DynamicLinearTimingGenerator,
    DynamicOneShotTimingGenerator,
    LinearTimingGenerator,
    NullTimingGenerator,
    OneShotTimingGenerator,
)

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_NullTimingGenerator():
    assert (
        NullTimingGenerator()._cpp_expression(gencontext)
        == "tcspc::null_timing_generator<tcspc::default_numeric_traits>()"
    )


def test_OneShotTimingGenerator():
    code = OneShotTimingGenerator(50)._cpp_expression(gencontext)
    assert (
        "tcspc::one_shot_timing_generator<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::delay<" in code
    assert "(50)" in code


def test_OneShotTimingGenerator_param():
    gen = OneShotTimingGenerator(Param("d"))
    assert len(gen._parameters()) == 1
    assert "params.z_d" in gen._cpp_expression(gencontext)


def test_DynamicOneShotTimingGenerator():
    assert (
        DynamicOneShotTimingGenerator()._cpp_expression(gencontext)
        == "tcspc::dynamic_one_shot_timing_generator<tcspc::default_numeric_traits>()"
    )


def test_LinearTimingGenerator():
    code = LinearTimingGenerator(10, 20, 5)._cpp_expression(gencontext)
    assert (
        "tcspc::linear_timing_generator<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::delay<" in code
    assert "tcspc::arg::interval<" in code
    assert "tcspc::arg::count<std::size_t>" in code


def test_LinearTimingGenerator_params():
    gen = LinearTimingGenerator(Param("d"), Param("i"), Param("c"))
    assert len(gen._parameters()) == 3


def test_DynamicLinearTimingGenerator():
    assert (
        DynamicLinearTimingGenerator()._cpp_expression(gencontext)
        == "tcspc::dynamic_linear_timing_generator<tcspc::default_numeric_traits>()"
    )
