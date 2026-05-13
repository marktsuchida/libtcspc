# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import final

import pytest
from _test_helpers import _NamedEvent
from libtcspc._bucket_sources import RecyclingBucketSource
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _identifier_from_string,
    _size_type,
)
from libtcspc._events import BucketEvent
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import Batch
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Batch_accepts_only_matching_event():
    node = Batch(IntEvent)
    assert node._map_event_sets([(IntEvent,)]) == ((BucketEvent(IntEvent),),)
    with pytest.raises(ValueError):
        node._map_event_sets([(OtherEvent,)])


def test_Batch_default_batch_size_is_65536():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::size_t{65536uLL}" in code


def test_Batch_batch_size_int():
    node = Batch(IntEvent, batch_size=42)
    assert len(node._parameters()) == 0
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::size_t{42uLL}" in code


def test_Batch_batch_size_param():
    node = Batch(IntEvent, batch_size=Param("bs"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("bs"), _size_type)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert f"params.{_identifier_from_string('bs')}" in code


def test_Batch_buffer_provider_params_propagate():
    bp = RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc"))
    node = Batch(IntEvent, buffer_provider=bp)
    params = node._parameters()
    assert (Param("mbc"), _size_type) in params


def test_Batch_default_bucket_source_is_recycling():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "recycling_bucket_source<int," in code


def test_Batch_codegen_calls_tcspc_batch():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::batch<int>(" in code
    assert "DOWN" in code


@final
class _MockSink(PySink):
    def __init__(self, log: list[str]) -> None:
        self._log = log

    @override
    def handle(self, event) -> None:
        self._log.append(f"handle({event})")

    @override
    def flush(self) -> None:
        self._log.append("flush()")


def test_Batch_param_round_trip():
    g = Graph()
    g.add_node("b", Batch(IntEvent, batch_size=Param("bs")))
    cg = CompiledGraph(g, (IntEvent,))
    log: list[str] = []
    c = ExecutionContext(cg, {"bs": 2}, (_MockSink(log),))
    c.handle(10)
    assert log == []
    c.handle(20)
    assert log == ["handle([10 20])"]
