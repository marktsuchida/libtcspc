# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc import Graph
from libtcspc._access import AccessTag, _CountAccessorSpec
from libtcspc._acquisition_readers import PyAcquisitionReader
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _uint32_type,
)
from libtcspc._events import (
    BucketEvent,
    DetectionEvent,
    TimeCorrelatedDetectionEvent,
)
from libtcspc._execute import ExecutionContext
from libtcspc._numeric_traits import NumericTraits
from libtcspc._param import Param
from libtcspc._processors import (
    Acquire,
    Count,
    RecordAbstimeRange,
    RecordLast,
    RemoveTimeCorrelation,
    SelectNone,
    SinkAll,
)
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Count_event_set_is_preserved():
    node = Count(IntEvent, AccessTag("c"))
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_Count_accesses_wires_count_accessor_spec():
    tag = AccessTag("c")
    node = Count(IntEvent, tag)
    accesses = node._accesses()
    assert len(accesses) == 1
    ((got_tag, spec),) = accesses
    assert got_tag == tag
    assert isinstance(spec, _CountAccessorSpec)


def test_Count_codegen_calls_tcspc_count():
    node = Count(IntEvent, AccessTag("c"))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::count<int>(" in code
    assert "DOWN" in code


def test_Count_codegen_tracker():
    node = Count(IntEvent, AccessTag("c"))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert 'ctx->tracker<tcspc::count_accessor>("c")' in code


class _NBucketReader(PyAcquisitionReader):
    """Reader that emits exactly ``n`` non-empty buckets, then ends."""

    def __init__(self, n: int) -> None:
        self.remaining = n

    @override
    def __call__(self, buffer: np.ndarray):
        if self.remaining == 0:
            return None
        self.remaining -= 1
        buffer[:] = 0
        return buffer.size


def test_Count_access_returns_running_count():
    acq_tag = AccessTag("acq")
    cnt_tag = AccessTag("cnt")
    elem_type = _NamedEvent(_uint32_type)
    g = Graph()
    g.add_node(
        "acq",
        Acquire(elem_type, Param("reader"), None, 8, acq_tag),
    )
    g.add_node("cnt", Count(BucketEvent(elem_type), cnt_tag), upstream="acq")
    g.add_node("sink", SinkAll(), upstream="cnt")
    cg = CompiledGraph(g)
    ctx = ExecutionContext(cg, {"reader": _NBucketReader(3)})
    ctx.flush()
    assert ctx.access(cnt_tag).count() == 3


def test_RecordAbstimeRange_codegen_calls_tcspc_record_abstime_range():
    node = RecordAbstimeRange(AccessTag("r"))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert (
        "tcspc::record_abstime_range<tcspc::default_numeric_traits>(" in code
    )
    assert "DOWN" in code
    assert (
        "ctx->tracker<tcspc::record_abstime_range_accessor<"
        'tcspc::default_numeric_traits::abstime_type>>("r")' in code
    )


def test_RecordAbstimeRange_access_returns_min_and_max():
    tag = AccessTag("r")
    det = DetectionEvent()
    g = Graph()
    g.add_node("rec", RecordAbstimeRange(tag))
    g.add_node("sink", SinkAll(), upstream="rec")
    cg = CompiledGraph(g, (det,))
    ctx = ExecutionContext(cg)
    assert ctx.access(tag).min() is None
    assert ctx.access(tag).max() is None
    ctx.handle(det.value(abstime=10, channel=0))
    ctx.handle(det.value(abstime=5, channel=0))
    ctx.handle(det.value(abstime=20, channel=0))
    assert ctx.access(tag).min() == 5
    assert ctx.access(tag).max() == 20
    ctx.flush()


def test_RecordAbstimeRange_with_custom_numeric_traits():
    tag = AccessTag("r")
    nt = NumericTraits(abstime_type="int32")
    det = DetectionEvent(nt)
    g = Graph()
    g.add_node("rec", RecordAbstimeRange(tag, nt))
    g.add_node("sink", SinkAll(), upstream="rec")
    cg = CompiledGraph(g, (det,))
    ctx = ExecutionContext(cg)
    ctx.handle(det.value(abstime=7, channel=1))
    assert ctx.access(tag).min() == 7
    assert ctx.access(tag).max() == 7
    ctx.flush()


def test_RecordLast_rejects_non_value_event_type():
    with pytest.raises(TypeError):
        RecordLast(_NamedEvent(_CppTypeName("int")), AccessTag("r"))


def test_RecordLast_codegen_calls_tcspc_record_last():
    det = DetectionEvent()
    node = RecordLast(det, AccessTag("r"))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert f"tcspc::record_last<{det._cpp_type_name()}>(" in code
    assert "DOWN" in code
    assert (
        f'ctx->tracker<tcspc::record_last_accessor<{det._cpp_type_name()}>>("r")'
        in code
    )


def test_RecordLast_access_returns_last_event():
    tag = AccessTag("r")
    det = DetectionEvent()
    g = Graph()
    g.add_node("rec", RecordLast(det, tag))
    g.add_node("sink", SinkAll(), upstream="rec")
    cg = CompiledGraph(g, (det,))
    ctx = ExecutionContext(cg)
    assert ctx.access(tag).get() is None
    ctx.handle(det.value(abstime=10, channel=2))
    assert ctx.access(tag).get() == det.value(abstime=10, channel=2)
    ctx.handle(det.value(abstime=20, channel=3))
    assert ctx.access(tag).get() == det.value(abstime=20, channel=3)
    ctx.flush()


def test_RecordLast_records_internal_only_event_type():
    # The recorded type appears only on an internal edge (it is neither the
    # graph input nor reaches any sink), so its value wrapper must be
    # generated via the _value_event_types hook.
    tag = AccessTag("r")
    tcde = TimeCorrelatedDetectionEvent()
    det = DetectionEvent()
    g = Graph()
    g.add_node("rm", RemoveTimeCorrelation())
    g.add_node("rec", RecordLast(det, tag), upstream="rm")
    g.add_node("sel", SelectNone(), upstream="rec")
    g.add_node("sink", SinkAll(), upstream="sel")
    cg = CompiledGraph(g, (tcde,))
    ctx = ExecutionContext(cg)
    assert ctx.access(tag).get() is None
    ctx.handle(tcde.value(abstime=42, channel=2, difftime=7))
    assert ctx.access(tag).get() == det.value(abstime=42, channel=2)
    ctx.flush()
