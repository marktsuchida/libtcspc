# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc import Graph
from libtcspc._access import AccessTag, _AcquireAccessSpec
from libtcspc._acquisition_readers import (
    NullReader,
    PyAcquisitionReader,
    StuckReader,
)
from libtcspc._bucket_sources import PyBucketSource, RecyclingBucketSource
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _identifier_from_string,
    _size_type,
    _uint32_type,
)
from libtcspc._events import BucketEvent
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._param import Param
from libtcspc._processors import Acquire, SinkAll
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Acquire_NullReader():
    acq_tag = AccessTag("acq")
    g = Graph()
    g.add_node(
        "acq",
        Acquire(
            _NamedEvent(_uint32_type),
            NullReader(_NamedEvent(_uint32_type)),
            None,
            32768,
            acq_tag,
        ),
    )
    g.add_node("sink", SinkAll(), upstream="acq")
    cg = CompiledGraph(g)
    ctx = ExecutionContext(cg)
    ctx.flush()


def test_Acquire_StuckReader():
    acq_tag = AccessTag("acq")
    g = Graph()
    g.add_node(
        "acq",
        Acquire(
            _NamedEvent(_uint32_type),
            StuckReader(_NamedEvent(_uint32_type)),
            None,
            None,
            acq_tag,
        ),
    )
    g.add_node("sink", SinkAll(), upstream="acq")
    cg = CompiledGraph(g)
    ctx = ExecutionContext(cg)
    # flush() would hang if called here.
    ctx.access(acq_tag).halt()
    with pytest.raises(RuntimeError):
        ctx.flush()


class MockReader(PyAcquisitionReader):
    def __init__(self, count: int) -> None:
        self.count = count

    @override
    def __call__(self, buffer: np.ndarray):
        self.count -= 1
        if self.count == 0:
            return None
        buffer[:] = 42
        return buffer.size


def test_Acquire_PyAcquisitionReader():
    acq_tag = AccessTag("acq")
    g = Graph()
    g.add_node(
        "acq",
        Acquire(
            _NamedEvent(_uint32_type), Param("reader"), None, None, acq_tag
        ),
    )
    g.add_node("sink", SinkAll(), upstream="acq")
    cg = CompiledGraph(g)
    with pytest.raises(ValueError):
        ctx = ExecutionContext(cg)
    reader = MockReader(3)
    ctx = ExecutionContext(
        cg,
        {
            "reader": reader,
        },
    )
    ctx.flush()
    assert reader.count == 0


def _acquire(
    reader=None,
    batch_size: int | Param[int] | None = None,
    tag: str = "acq",
) -> Acquire:
    return Acquire(
        IntEvent,
        reader if reader is not None else NullReader(IntEvent),
        None,
        batch_size,
        AccessTag(tag),
    )


def test_Acquire_rejects_nonempty_input_set():
    node = _acquire()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_Acquire_output_event_set_is_bucket_of_event_type():
    node = _acquire()
    assert node._map_event_sets([()]) == ((BucketEvent(IntEvent),),)


def test_Acquire_accesses_wires_acquire_access_spec():
    node = _acquire(tag="acq")
    assert node._accesses() == ((AccessTag("acq"), _AcquireAccessSpec),)


def test_Acquire_parameters_default():
    node = _acquire()
    assert len(node._parameters()) == 0


def test_Acquire_parameters_with_param_reader():
    node = _acquire(reader=Param("reader"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0][0] == Param("reader")
    assert "std::function<" in params[0][1]
    assert "std::optional<std::size_t>" in params[0][1]


def test_Acquire_parameters_with_param_batch_size():
    node = _acquire(batch_size=Param("bs"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("bs"), _size_type)


def test_Acquire_codegen_calls_acquire_with_event_type():
    node = _acquire()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::acquire<int>(" in code
    assert "DOWN" in code


def test_Acquire_codegen_includes_batch_size_arg():
    node_default = _acquire()
    code_default = node_default._cpp_expression(
        gencontext, [_CppExpression("DOWN")]
    )
    assert "tcspc::arg::batch_size{std::size_t{65536uLL}}" in code_default

    node_param = _acquire(batch_size=Param("bs"))
    code_param = node_param._cpp_expression(
        gencontext, [_CppExpression("DOWN")]
    )
    assert f"params.{_identifier_from_string('bs')}" in code_param


def test_Acquire_codegen_tracker():
    node = _acquire(tag="the-tag")
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert 'ctx->tracker<tcspc::acquire_access>("the-tag")' in code


def test_Acquire_buffer_provider_params_propagate():
    bp = RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc"))
    node = Acquire(IntEvent, NullReader(IntEvent), bp, None, AccessTag("acq"))
    params = node._parameters()
    assert (Param("mbc"), _size_type) in params


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


def _run_acquire_with_py_bucket_source(
    reader: PyAcquisitionReader,
    source: PyBucketSource,
    sink: PySink,
    batch_size: int,
) -> None:
    g = Graph()
    g.add_node(
        "acq",
        Acquire(
            _NamedEvent(_uint32_type),
            Param("reader"),
            Param("bs"),
            batch_size,
            AccessTag("acq"),
        ),
    )
    cg = CompiledGraph(g)
    ctx = ExecutionContext(cg, {"reader": reader, "bs": source}, (sink,))
    ctx.flush()


def test_Acquire_PyBucketSource_zero_copy_views():
    source = MockBucketSource()
    sink = RecordingSink()
    # Second read is partial (length 2 < batch_size 4) to exercise trimming.
    reader = SequenceReader([[10, 11, 12, 13], [20, 21]])
    _run_acquire_with_py_bucket_source(reader, source, sink, batch_size=4)

    assert len(sink.received) == 2
    assert list(sink.received[0]) == [10, 11, 12, 13]
    assert list(sink.received[1]) == [20, 21]

    # Each delivered array is a zero-copy view of a buffer the source handed
    # out (not a copy).
    for arr in sink.received:
        assert any(np.shares_memory(arr, b) for b in source.buffers)


def test_Acquire_PyBucketSource_view_reflects_source_buffer_mutation():
    source = MockBucketSource()
    sink = RecordingSink()
    reader = SequenceReader([[1, 2, 3, 4]])
    _run_acquire_with_py_bucket_source(reader, source, sink, batch_size=4)

    assert len(sink.received) == 1
    received = sink.received[0]
    backing = next(b for b in source.buffers if np.shares_memory(received, b))
    backing[0] = 999
    assert received[0] == 999


def test_Acquire_PyBucketSource_too_small_buffer_raises():
    class TooSmallBucketSource(PyBucketSource):
        @override
        def bucket_of_size(self, size: int):
            return np.empty(max(size - 1, 0), dtype=np.uint32)

    source = TooSmallBucketSource()
    sink = RecordingSink()
    reader = SequenceReader([[1, 2, 3, 4]])
    with pytest.raises(RuntimeError):
        _run_acquire_with_py_bucket_source(reader, source, sink, batch_size=4)
