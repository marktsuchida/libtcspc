# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _string_type, _uint64_type
from libtcspc._param import Param
from libtcspc._streams import BinaryFileInputStream

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_BinaryFileInputStream_default():
    bfis = BinaryFileInputStream("some_file")
    assert len(bfis._parameters()) == 0
    assert "tcspc::binary_file_input_stream" in bfis._cpp_expression(
        gencontext
    )
    assert '"some_file"' in bfis._cpp_expression(gencontext)


def test_BinaryFileInputStream_filename_param():
    bfis = BinaryFileInputStream(Param("fname"))
    assert len(bfis._parameters()) == 1
    assert bfis._parameters()[0] == (Param("fname"), _string_type)
    assert "params.fname" in bfis._cpp_expression(gencontext)


def test_BinaryFileInputStream_start_offset():
    bfis = BinaryFileInputStream("some_file", start_offset=42)
    assert "42uLL" in bfis._cpp_expression(gencontext)


def test_BinaryFileInputStream_start_offset_param():
    bfis = BinaryFileInputStream("some_file", start_offset=Param("stoff", 42))
    assert len(bfis._parameters()) == 1
    assert bfis._parameters()[0] == (Param("stoff", 42), _uint64_type)
    assert "params.stoff" in bfis._cpp_expression(gencontext)


def test_BinaryFileInputStream_start_offset_negative_is_error():
    with pytest.raises(ValueError):
        BinaryFileInputStream("some_file", start_offset=-1)

    with pytest.raises(ValueError):
        BinaryFileInputStream("some_file", start_offset=Param("stoff", -1))
