# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import (
    BHSPCEvent,
    BulkCountsEvent,
    DataLostEvent,
    MarkerEvent,
    TimeCorrelatedDetectionEvent,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._processors import DecodeBHSPCWithIntensityCounter

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodeBHSPCWithIntensityCounter_accepts_only_BHSPCEvent():
    node = DecodeBHSPCWithIntensityCounter()
    out = node._map_event_sets([(BHSPCEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeBHSPCWithIntensityCounter_output_event_set():
    dt = NumericTraits()
    node = DecodeBHSPCWithIntensityCounter(dt)
    assert node._map_event_sets([(BHSPCEvent(),)]) == (
        (
            BulkCountsEvent(dt),
            DataLostEvent(dt),
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeBHSPCWithIntensityCounter_codegen():
    node = DecodeBHSPCWithIntensityCounter()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_bh_spc_with_intensity_counter<" in code
    assert "DOWN" in code


def test_DecodeBHSPCWithIntensityCounter_compiles_end_to_end():
    from libtcspc._compile import CompiledGraph
    from libtcspc._execute import ExecutionContext
    from libtcspc._graph import Graph
    from libtcspc._processors import SinkAll

    g = Graph()
    g.add_node("dec", DecodeBHSPCWithIntensityCounter())
    g.add_node(None, SinkAll(), upstream="dec")
    cg = CompiledGraph(g, (BHSPCEvent(),))
    ctx = ExecutionContext(cg)
    ctx.flush()
