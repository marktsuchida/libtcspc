# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._access import AccessTag, _UniqueBinMapperAccessSpec
from libtcspc._bin_mappers import (
    LinearBinMapper,
    PowerOf2BinMapper,
    UniqueBinMapper,
)
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _size_type
from libtcspc._param import Param

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_PowerOf2BinMapper():
    code = PowerOf2BinMapper(12, 8)._cpp_expression(gencontext)
    assert "tcspc::power_of_2_bin_mapper<12, 8, false," in code


def test_PowerOf2BinMapper_flip():
    code = PowerOf2BinMapper(12, 8, flip=True)._cpp_expression(gencontext)
    assert "tcspc::power_of_2_bin_mapper<12, 8, true," in code


def test_LinearBinMapper():
    code = LinearBinMapper(0, 1, 100)._cpp_expression(gencontext)
    assert "tcspc::linear_bin_mapper<tcspc::default_numeric_traits>(" in code
    assert "tcspc::arg::offset<" in code
    assert "tcspc::arg::bin_width<" in code
    assert "tcspc::arg::max_bin_index<" in code
    assert "tcspc::arg::clamp{false}" in code


def test_LinearBinMapper_clamp():
    code = LinearBinMapper(0, 1, 100, clamp=True)._cpp_expression(gencontext)
    assert "tcspc::arg::clamp{true}" in code


def test_LinearBinMapper_param():
    bm = LinearBinMapper(Param("off"), 1, 100)
    params = bm._parameters()
    assert len(params) == 1
    assert params[0][0] == Param("off")
    assert "params.z_off" in bm._cpp_expression(gencontext)


def test_UniqueBinMapper_accesses():
    tag = AccessTag("u")
    bm = UniqueBinMapper(tag, 100)
    accesses = bm._accesses()
    assert len(accesses) == 1
    ((got_tag, spec),) = accesses
    assert got_tag == tag
    assert isinstance(spec, _UniqueBinMapperAccessSpec)


def test_UniqueBinMapper_codegen():
    code = UniqueBinMapper(AccessTag("u"), 100)._cpp_expression(gencontext)
    assert "tcspc::unique_bin_mapper<tcspc::default_numeric_traits>(" in code
    assert (
        "ctx->tracker<tcspc::unique_bin_mapper_access<"
        'tcspc::default_numeric_traits::datapoint_type>>("u")' in code
    )
    assert "tcspc::arg::max_bin_index<" in code


def test_UniqueBinMapper_param():
    bm = UniqueBinMapper(AccessTag("u"), Param("m"))
    params = bm._parameters()
    assert len(params) == 1
    assert params[0][0] == Param("m")
    assert "params.z_m" in bm._cpp_expression(gencontext)


def test_UniqueBinMapperAccessSpec_methods():
    spec = _UniqueBinMapperAccessSpec()
    assert "values" in spec.cpp_methods()
    assert spec.py_class_name() == "UniqueBinMapperAccess"


def test_no_params_when_literal():
    assert len(PowerOf2BinMapper(12, 8)._parameters()) == 0
    assert len(LinearBinMapper(0, 1, 100)._parameters()) == 0
    _ = _size_type
