# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
from _test_helpers import _NamedEvent
from libtcspc import Graph
from libtcspc._bucket_sources import PyBucketSource
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _uint32_type,
)
from libtcspc._events import BucketEvent, ConstBucketEvent, WarningEvent
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._param import Param
from libtcspc._processors import CopyToBuckets, SinkAll

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_CopyToBuckets_output_event_set():
    node = CopyToBuckets(IntEvent)
    assert node._map_event_sets([(ConstBucketEvent(IntEvent),)]) == (
        (BucketEvent(IntEvent),),
    )


def test_CopyToBuckets_passes_through_other_events():
    node = CopyToBuckets(IntEvent)
    (out,) = node._map_event_sets(
        [(ConstBucketEvent(IntEvent), WarningEvent())]
    )
    assert BucketEvent(IntEvent) in out
    assert WarningEvent() in out


def test_CopyToBuckets_parameters_default_is_empty():
    node = CopyToBuckets(IntEvent)
    assert len(node._parameters()) == 0


def test_CopyToBuckets_parameters_with_param_buffer_provider():
    node = CopyToBuckets(IntEvent, Param("bs"))
    params = node._parameters()
    assert params == ((Param("bs"), _CppTypeName("nanobind::object")),)


def test_CopyToBuckets_codegen_calls_copy_to_buckets():
    node = CopyToBuckets(IntEvent)
    code = node._relay_cpp_expression(gencontext, _CppExpression("DOWN"))
    assert "tcspc::copy_to_buckets<tcspc::bucket<int const>, int>(" in code
    assert "DOWN" in code


def test_CopyToBuckets_compiles():
    g = Graph()
    g.add_node("copy", CopyToBuckets(_NamedEvent(_uint32_type)))
    g.add_node("sink", SinkAll(), upstream="copy")
    cg = CompiledGraph(g, [ConstBucketEvent(_NamedEvent(_uint32_type))])
    ctx = ExecutionContext(cg)
    ctx.flush()


class MockBucketSource(PyBucketSource):
    def __init__(self) -> None:
        self.buffers: list[np.ndarray] = []

    def bucket_of_size(self, size: int):
        b = np.empty(size, dtype=np.uint32)
        self.buffers.append(b)
        return b


class RecordingSink(PySink):
    def __init__(self) -> None:
        self.received: list[np.ndarray] = []

    def handle(self, event) -> None:
        self.received.append(event)

    def flush(self) -> None:
        pass


def test_CopyToBuckets_PyBucketSource_single_copy():
    source = MockBucketSource()
    sink = RecordingSink()
    g = Graph()
    g.add_node("copy", CopyToBuckets(_NamedEvent(_uint32_type), Param("bs")))
    cg = CompiledGraph(g, [ConstBucketEvent(_NamedEvent(_uint32_type))])
    ctx = ExecutionContext(cg, {"bs": source}, (sink,))
    inp = np.array([1, 2, 3, 4], dtype=np.uint32)
    ctx.handle(inp)
    ctx.flush()

    assert [list(a) for a in sink.received] == [[1, 2, 3, 4]]
    out = sink.received[0]
    # The one and only copy is into a bucket the source handed out.
    assert any(np.shares_memory(out, b) for b in source.buffers)
    # The output is not a view of the input array.
    assert not np.shares_memory(out, inp)
