# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _bool_type,
    _CppIdentifier,
    _identifier_from_string,
    _string_type,
    _uint64_type,
)
from libtcspc._param import Param
from libtcspc._streams import (
    BinaryFileInputStream,
    BinaryFileOutputStream,
    NullInputStream,
    NullOutputStream,
)

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_NullInputStream_codegen():
    nis = NullInputStream()
    assert len(nis._parameters()) == 0
    assert "tcspc::null_input_stream()" in nis._cpp_expression(gencontext)


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
    assert (
        f"params.{_identifier_from_string('fname')}"
        in bfis._cpp_expression(gencontext)
    )


def test_BinaryFileInputStream_start_offset():
    bfis = BinaryFileInputStream("some_file", start_offset=42)
    assert "42uLL" in bfis._cpp_expression(gencontext)


def test_BinaryFileInputStream_start_offset_param():
    bfis = BinaryFileInputStream("some_file", start_offset=Param("stoff", 42))
    assert len(bfis._parameters()) == 1
    assert bfis._parameters()[0] == (Param("stoff", 42), _uint64_type)
    assert (
        f"params.{_identifier_from_string('stoff')}"
        in bfis._cpp_expression(gencontext)
    )


def test_BinaryFileInputStream_start_offset_negative_is_error():
    with pytest.raises(ValueError):
        BinaryFileInputStream("some_file", start_offset=-1)

    with pytest.raises(ValueError):
        BinaryFileInputStream("some_file", start_offset=Param("stoff", -1))


def test_NullOutputStream_codegen():
    nos = NullOutputStream()
    assert len(nos._parameters()) == 0
    assert "tcspc::null_output_stream()" in nos._cpp_expression(gencontext)


def test_BinaryFileOutputStream_default():
    bfos = BinaryFileOutputStream("some_file")
    assert len(bfos._parameters()) == 0
    code = bfos._cpp_expression(gencontext)
    assert "tcspc::binary_file_output_stream" in code
    assert '"some_file"' in code
    assert "tcspc::arg::truncate<bool>{false}" in code
    assert "tcspc::arg::append<bool>{false}" in code


def test_BinaryFileOutputStream_filename_param():
    bfos = BinaryFileOutputStream(Param("fname"))
    assert len(bfos._parameters()) == 1
    assert bfos._parameters()[0] == (Param("fname"), _string_type)
    assert (
        f"params.{_identifier_from_string('fname')}"
        in bfos._cpp_expression(gencontext)
    )


def test_BinaryFileOutputStream_truncate_append_literals():
    bfos = BinaryFileOutputStream("some_file", truncate=True, append=True)
    assert len(bfos._parameters()) == 0
    code = bfos._cpp_expression(gencontext)
    assert "tcspc::arg::truncate<bool>{true}" in code
    assert "tcspc::arg::append<bool>{true}" in code


def test_BinaryFileOutputStream_truncate_append_params():
    bfos = BinaryFileOutputStream(
        "some_file", truncate=Param("tr"), append=Param("ap")
    )
    params = bfos._parameters()
    assert (Param("tr"), _bool_type) in params
    assert (Param("ap"), _bool_type) in params
    code = bfos._cpp_expression(gencontext)
    assert f"params.{_identifier_from_string('tr')}" in code
    assert f"params.{_identifier_from_string('ap')}" in code
