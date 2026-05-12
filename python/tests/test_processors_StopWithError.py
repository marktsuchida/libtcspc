# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from pathlib import Path

import pytest
from _test_helpers import _NamedEvent
from libtcspc import Graph
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _string_type,
    _uint32_type,
)
from libtcspc._events import BucketEvent, WarningEvent
from libtcspc._execute import ExecutionContext
from libtcspc._param import Param
from libtcspc._processors import (
    NullSink,
    ReadBinaryStream,
    StopWithError,
    Unbatch,
)
from libtcspc._streams import BinaryFileInputStream

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_StopWithError_removes_configured_event_from_output_set():
    node = StopWithError((IntEvent,), "msg")
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == ((OtherEvent,),)


def test_StopWithError_keeps_other_events():
    node = StopWithError((IntEvent,), "msg")
    assert node._map_event_sets([(OtherEvent,)]) == ((OtherEvent,),)


def test_StopWithError_message_prefix_str():
    node = StopWithError((IntEvent,), "my prefix")
    assert len(node._parameters()) == 0
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert '"my prefix"' in code


def test_StopWithError_message_prefix_param():
    node = StopWithError((IntEvent,), Param("msg"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("msg"), _string_type)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "params.msg" in code


def test_StopWithError_codegen_calls_tcspc_stop_with_error():
    node = StopWithError((IntEvent, OtherEvent), "msg")
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::stop_with_error<" in code
    assert "tcspc::type_list<int, long>" in code
    assert "DOWN" in code


def test_StopWithError_raises_RuntimeError_on_event(tmp_path: Path) -> None:
    elem_type = _NamedEvent(_uint32_type)
    # File of 3 bytes is shorter than sizeof(uint32_t)=4, so ReadBinaryStream
    # emits a WarningEvent at end-of-stream, which StopWithError converts into
    # an error.
    path = tmp_path / "short.bin"
    path.write_bytes(b"\x00\x00\x00")

    g = Graph()
    g.add_sequence(
        [
            (
                "reader",
                ReadBinaryStream(elem_type, BinaryFileInputStream(str(path))),
            ),
            StopWithError((WarningEvent(),), "halt"),
            Unbatch(BucketEvent(elem_type)),
        ]
    )
    g.add_node(None, NullSink(), upstream=g.outputs()[0][0])

    cg = CompiledGraph(g)
    ctx = ExecutionContext(cg)
    with pytest.raises(RuntimeError):
        ctx.flush()
