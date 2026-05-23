# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc import Graph
from libtcspc._bucket_sources import PyBucketSource
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _size_type,
    _uint32_type,
)
from libtcspc._events import BucketEvent, ConstBucketEvent, WarningEvent
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._param import Param
from libtcspc._processors import CopyToFullBuckets, SinkAll

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_CopyToFullBuckets_output_event_sets():
    node = CopyToFullBuckets(IntEvent)
    assert node._map_event_sets([(ConstBucketEvent(IntEvent),)]) == (
        (ConstBucketEvent(IntEvent),),
        (BucketEvent(IntEvent),),
    )


def test_CopyToFullBuckets_passes_through_other_events_on_live():
    node = CopyToFullBuckets(IntEvent)
    live, batch = node._map_event_sets(
        [(ConstBucketEvent(IntEvent), WarningEvent())]
    )
    assert ConstBucketEvent(IntEvent) in live
    assert WarningEvent() in live
    assert batch == (BucketEvent(IntEvent),)


def test_CopyToFullBuckets_rejects_wrong_number_of_inputs():
    node = CopyToFullBuckets(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets(
            [(ConstBucketEvent(IntEvent),), (ConstBucketEvent(IntEvent),)]
        )


def test_CopyToFullBuckets_parameters_with_param_batch_size():
    node = CopyToFullBuckets(IntEvent, batch_size=Param("bs"))
    params = node._parameters()
    assert (Param("bs"), _size_type) in params


def test_CopyToFullBuckets_parameters_with_param_buffer_provider():
    node = CopyToFullBuckets(IntEvent, Param("bs"))
    params = node._parameters()
    assert (Param("bs"), _CppTypeName("nanobind::object")) in params


def test_CopyToFullBuckets_codegen_calls_copy_to_full_buckets():
    node = CopyToFullBuckets(IntEvent)
    code = node._cpp_expression(
        gencontext, [_CppExpression("LIVE"), _CppExpression("BATCH")]
    )
    assert (
        "tcspc::copy_to_full_buckets<tcspc::bucket<int const>, int>(" in code
    )
    assert "tcspc::arg::batch_size" in code
    assert "LIVE" in code
    assert "BATCH" in code


def test_CopyToFullBuckets_compiles():
    g = Graph()
    g.add_node("copy", CopyToFullBuckets(_NamedEvent(_uint32_type)))
    g.add_node("live", SinkAll(), upstream=("copy", "live"))
    g.add_node("batch", SinkAll(), upstream=("copy", "batch"))
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


def test_CopyToFullBuckets_PyBucketSource_live_views_are_readonly_zero_copy():
    source = MockBucketSource()
    live = RecordingSink()
    batch = RecordingSink()

    g = Graph()
    g.add_node(
        "copy",
        CopyToFullBuckets(
            _NamedEvent(_uint32_type), Param("bs"), batch_size=4
        ),
    )
    cg = CompiledGraph(g, [ConstBucketEvent(_NamedEvent(_uint32_type))])
    # Unconnected outputs become external outputs ordered (live, batch).
    ctx = ExecutionContext(cg, {"bs": source}, (live, batch))
    ctx.handle(np.array([10, 11, 12, 13], dtype=np.uint32))
    ctx.handle(np.array([20, 21], dtype=np.uint32))
    ctx.flush()

    assert [list(a) for a in live.received] == [[10, 11, 12, 13], [20, 21]]
    assert [list(a) for a in batch.received] == [[10, 11, 12, 13], [20, 21]]

    # Each live array is a zero-copy view of a buffer the source handed out.
    for arr in live.received:
        assert any(np.shares_memory(arr, b) for b in source.buffers)

    # Each live array is read-only (a const shared view).
    for arr in live.received:
        assert arr.flags.writeable is False

    # By contrast, the batch arrays are writable.
    for arr in batch.received:
        assert arr.flags.writeable is True
