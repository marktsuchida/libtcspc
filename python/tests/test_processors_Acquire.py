# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import numpy as np
import pytest
from libtcspc import Graph
from libtcspc._access import AccessTag
from libtcspc._acquisition_readers import (
    NullReader,
    PyAcquisitionReader,
    StuckReader,
)
from libtcspc._compile import compile_graph
from libtcspc._cpp_utils import uint32_type
from libtcspc._events import EventType
from libtcspc._execute import create_execution_context
from libtcspc._param import Param
from libtcspc._processors import Acquire, NullSink
from typing_extensions import override


def test_Acquire_NullReader():
    acq_tag = AccessTag("acq")
    g = Graph()
    g.add_node(
        "acq",
        Acquire(
            EventType(uint32_type),
            NullReader(EventType(uint32_type)),
            None,
            32768,
            acq_tag,
        ),
    )
    g.add_node("sink", NullSink(), upstream="acq")
    cg = compile_graph(g)
    ctx = create_execution_context(cg)
    ctx.flush()


def test_Acquire_StuckReader():
    acq_tag = AccessTag("acq")
    g = Graph()
    g.add_node(
        "acq",
        Acquire(
            EventType(uint32_type),
            StuckReader(EventType(uint32_type)),
            None,
            None,
            acq_tag,
        ),
    )
    g.add_node("sink", NullSink(), upstream="acq")
    cg = compile_graph(g)
    ctx = create_execution_context(cg)
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
        Acquire(EventType(uint32_type), Param("reader"), None, None, acq_tag),
    )
    g.add_node("sink", NullSink(), upstream="acq")
    cg = compile_graph(g)
    with pytest.raises(ValueError):
        ctx = create_execution_context(cg)
    reader = MockReader(3)
    ctx = create_execution_context(
        cg,
        {
            "reader": reader,
        },
    )
    ctx.flush()
    assert reader.count == 0
