# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc import Graph
from libtcspc._access import AccessTag, _AcquireAccessSpec
from libtcspc._acquisition_readers import NullReader, PyAcquisitionReader
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
from libtcspc._events import BucketEvent, ConstBucketEvent
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._param import Param
from libtcspc._processors import AcquireFullBuckets, SinkAll
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def _acquire_full_buckets(
    reader=None,
    batch_size: int | Param[int] | None = None,
    tag: str = "acq",
) -> AcquireFullBuckets:
    return AcquireFullBuckets(
        IntEvent,
        reader if reader is not None else NullReader(IntEvent),
        None,
        batch_size,
        AccessTag(tag),
    )


def test_AcquireFullBuckets_NullReader():
    acq_tag = AccessTag("acq")
    g = Graph()
    g.add_node(
        "acq",
        AcquireFullBuckets(
            _NamedEvent(_uint32_type),
            NullReader(_NamedEvent(_uint32_type)),
            None,
            32768,
            acq_tag,
        ),
    )
    g.add_node("live", SinkAll(), upstream=("acq", "live"))
    g.add_node("batch", SinkAll(), upstream=("acq", "batch"))
    cg = CompiledGraph(g)
    ctx = ExecutionContext(cg)
    ctx.flush()


def test_AcquireFullBuckets_rejects_nonempty_input_set():
    node = _acquire_full_buckets()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_AcquireFullBuckets_output_event_sets():
    node = _acquire_full_buckets()
    assert node._map_event_sets([set()]) == (
        (ConstBucketEvent(IntEvent),),
        (BucketEvent(IntEvent),),
    )


def test_AcquireFullBuckets_accesses_wires_acquire_access_spec():
    node = _acquire_full_buckets(tag="acq")
    assert node._accesses() == ((AccessTag("acq"), _AcquireAccessSpec),)


def test_AcquireFullBuckets_parameters_with_param_reader():
    node = _acquire_full_buckets(reader=Param("reader"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0][0] == Param("reader")
    assert "std::function<" in params[0][1]
    assert "std::optional<std::size_t>" in params[0][1]


def test_AcquireFullBuckets_parameters_with_param_batch_size():
    node = _acquire_full_buckets(batch_size=Param("bs"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("bs"), _size_type)


def test_AcquireFullBuckets_codegen_calls_acquire_full_buckets():
    node = _acquire_full_buckets()
    code = node._cpp_expression(
        gencontext, [_CppExpression("LIVE"), _CppExpression("BATCH")]
    )
    assert "tcspc::acquire_full_buckets<int>(" in code
    assert "LIVE" in code
    assert "BATCH" in code


class SequenceReader(PyAcquisitionReader):
    """Reader that emits a fixed sequence of reads, then end of stream."""

    def __init__(self, reads: list[list[int]]) -> None:
        self._reads = [np.asarray(r, dtype=np.uint32) for r in reads]

    @override
    def __call__(self, buffer: np.ndarray):
        if not self._reads:
            return None
        data = self._reads.pop(0)
        buffer[: data.size] = data
        return int(data.size)


class MockBucketSource(PyBucketSource):
    def __init__(self) -> None:
        self.buffers: list[np.ndarray] = []

    @override
    def bucket_of_size(self, size: int):
        b = np.empty(size, dtype=np.uint32)
        self.buffers.append(b)
        return b


class RecordingSink(PySink):
    def __init__(self) -> None:
        self.received: list[np.ndarray] = []

    @override
    def handle(self, event) -> None:
        self.received.append(event)

    @override
    def flush(self) -> None:
        pass


def test_AcquireFullBuckets_PyBucketSource_live_views_are_readonly_zero_copy():
    reader = SequenceReader([[10, 11, 12, 13], [20, 21]])
    source = MockBucketSource()
    live = RecordingSink()
    batch = RecordingSink()

    g = Graph()
    g.add_node(
        "acq",
        AcquireFullBuckets(
            _NamedEvent(_uint32_type),
            Param("reader"),
            Param("bs"),
            4,
            AccessTag("acq"),
        ),
    )
    cg = CompiledGraph(g)
    # Unconnected outputs become external outputs ordered (live, batch).
    ctx = ExecutionContext(cg, {"reader": reader, "bs": source}, (live, batch))
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
