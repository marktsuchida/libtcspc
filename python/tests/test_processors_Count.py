# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
from _test_helpers import _NamedEvent
from libtcspc import Graph
from libtcspc._access import AccessTag, _CountAccessSpec
from libtcspc._acquisition_readers import PyAcquisitionReader
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _uint32_type,
)
from libtcspc._events import BucketEvent
from libtcspc._execute import ExecutionContext
from libtcspc._param import Param
from libtcspc._processors import Acquire, Count, NullSink
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


def test_Count_accesses_wires_count_access_spec():
    tag = AccessTag("c")
    node = Count(IntEvent, tag)
    assert node._accesses() == ((tag, _CountAccessSpec),)


def test_Count_codegen_calls_tcspc_count():
    node = Count(IntEvent, AccessTag("c"))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::count<int>(" in code
    assert "DOWN" in code


def test_Count_codegen_tracker():
    node = Count(IntEvent, AccessTag("c"))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert 'ctx->tracker<tcspc::count_access>("c")' in code


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
    g.add_node("sink", NullSink(), upstream="cnt")
    cg = CompiledGraph(g)
    ctx = ExecutionContext(cg, {"reader": _NBucketReader(3)})
    ctx.flush()
    assert ctx.access(cnt_tag).count() == 3
