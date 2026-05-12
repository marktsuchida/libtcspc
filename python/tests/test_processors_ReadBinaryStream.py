# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _size_type,
    _string_type,
    _uint64_type,
)
from libtcspc._events import BucketEvent
from libtcspc._param import Param
from libtcspc._processors import ReadBinaryStream
from libtcspc._streams import BinaryFileInputStream

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def _read(
    max_length: int | Param[int] | None = None,
    read_granularity_bytes: int | Param[int] = 65536,
    stream: BinaryFileInputStream | None = None,
) -> ReadBinaryStream:
    return ReadBinaryStream(
        IntEvent,
        stream if stream is not None else BinaryFileInputStream("some_file"),
        max_length,
        None,
        read_granularity_bytes,
    )


def test_ReadBinaryStream_rejects_nonempty_input_set():
    node = _read()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_ReadBinaryStream_output_event_set():
    node = _read()
    assert node._map_event_sets([()]) == ((BucketEvent(IntEvent),),)


def test_ReadBinaryStream_default_max_length_is_unlimited():
    node = _read()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::numeric_limits<tcspc::u64>::max()" in code


def test_ReadBinaryStream_max_length_int():
    node = _read(max_length=1024)
    names = [p.name for p, _ in node._parameters()]
    assert "ml" not in names
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::u64{1024uLL}" in code


def test_ReadBinaryStream_max_length_param():
    node = _read(max_length=Param("ml"))
    params = node._parameters()
    assert (Param("ml"), _uint64_type) in params
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "params.ml" in code


def test_ReadBinaryStream_granularity_param():
    node = _read(read_granularity_bytes=Param("g"))
    params = node._parameters()
    assert (Param("g"), _size_type) in params
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "params.g" in code


def test_ReadBinaryStream_propagates_stream_parameters():
    node = _read(stream=BinaryFileInputStream(Param("fname")))
    params = node._parameters()
    assert (Param("fname"), _string_type) in params


def test_ReadBinaryStream_codegen_calls_read_binary_stream():
    node = _read()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::read_binary_stream<int>(" in code
    assert "tcspc::arg::max_length<tcspc::u64>{" in code
    assert "tcspc::arg::granularity<std::size_t>{" in code
    assert "DOWN" in code
