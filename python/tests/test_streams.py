# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._codegen import CodeGenerationContext
from libtcspc._cpp_utils import CppIdentifier, CppTypeName
from libtcspc._param import Param
from libtcspc._streams import BinaryFileInputStream

gencontext = CodeGenerationContext(
    CppIdentifier("ctx"), CppIdentifier("params")
)


def test_BinaryFileInputStream_default():
    bfis = BinaryFileInputStream("some_file")
    assert len(bfis.parameters()) == 0
    assert "tcspc::binary_file_input_stream" in bfis.cpp_expression(gencontext)
    assert '"some_file"' in bfis.cpp_expression(gencontext)


def test_BinaryFileInputStream_filename_param():
    bfis = BinaryFileInputStream(Param("fname"))
    assert len(bfis.parameters()) == 1
    assert bfis.parameters()[0] == (Param("fname"), CppTypeName("std::string"))
    assert "params.fname" in bfis.cpp_expression(gencontext)


def test_BinaryFileInputStream_start_offset():
    bfis = BinaryFileInputStream("some_file", start_offset=42)
    assert "42uLL" in bfis.cpp_expression(gencontext)


def test_BinaryFileInputStream_start_offset_param():
    bfis = BinaryFileInputStream("some_file", start_offset=Param("stoff", 42))
    assert len(bfis.parameters()) == 1
    assert bfis.parameters()[0] == (
        Param("stoff", 42),
        CppTypeName("tcspc::u64"),
    )
    assert "params.stoff" in bfis.cpp_expression(gencontext)


def test_BinaryFileInputStream_start_offset_negative_is_error():
    with pytest.raises(ValueError):
        BinaryFileInputStream("some_file", start_offset=-1)

    with pytest.raises(ValueError):
        BinaryFileInputStream("some_file", start_offset=Param("stoff", -1))
