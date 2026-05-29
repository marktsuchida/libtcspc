# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from libtcspc._access import AccessTag
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier
from libtcspc._events import DetectionEvent, MarkerEvent, TimeReachedEvent
from libtcspc._execute import ExecutionContext
from libtcspc._graph import Graph
from libtcspc._processors import (
    Count,
    Demultiplex,
    Multiplex,
    SelectAll,
    SinkAll,
)

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]

A = DetectionEvent()
B = MarkerEvent()
C = TimeReachedEvent()


def test_Multiplex_requires_at_least_one_event_type():
    with pytest.raises(ValueError, match="at least one"):
        Multiplex()


def test_Multiplex_output_is_single_variant_event():
    node = Multiplex(A, B)
    (out,) = node._map_event_sets([(A, B)])
    assert out == (tcspc.VariantEvent(A, B),)


def test_Multiplex_output_variant_spans_full_declared_list():
    # Even if the input carries a subset, the output variant spans all of the
    # declared event types.
    node = Multiplex(A, B)
    (out,) = node._map_event_sets([(A,)])
    assert out == (tcspc.VariantEvent(A, B),)


def test_Multiplex_rejects_input_event_not_in_declared_list():
    node = Multiplex(A, B)
    with pytest.raises(ValueError, match="not accepted by Multiplex"):
        node._map_event_sets([(A, C)])


def test_Multiplex_codegen_calls_tcspc_multiplex():
    node = Multiplex(A, B)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::multiplex<" in code
    assert (
        f"tcspc::type_list<{A._cpp_type_name()}, {B._cpp_type_name()}>" in code
    )
    assert "DOWN" in code


def test_Demultiplex_unwraps_variant_into_members_in_order():
    node = Demultiplex()
    (out,) = node._map_event_sets([(tcspc.VariantEvent(A, B),)])
    assert out == (A, B)


def test_Demultiplex_preserves_member_order():
    node = Demultiplex()
    (out,) = node._map_event_sets([(tcspc.VariantEvent(B, A),)])
    assert out == (B, A)


def test_Demultiplex_rejects_non_variant_input_event():
    node = Demultiplex()
    with pytest.raises(ValueError, match="not accepted by Demultiplex"):
        node._map_event_sets([(A,)])


def test_Demultiplex_codegen_calls_tcspc_demultiplex():
    code = Demultiplex()._cpp_expression(gencontext, DOWN)
    assert code == "tcspc::demultiplex(DOWN)"


def test_Multiplex_then_Demultiplex_round_trips_event_set():
    (mux_out,) = Multiplex(A, B)._map_event_sets([(A, B)])
    (demux_out,) = Demultiplex()._map_event_sets([mux_out])
    # Order is preserved (EventType is unhashable by design, so compare as a
    # tuple rather than a set).
    assert demux_out == (A, B)


def test_Multiplex_Demultiplex_round_trip_compiles_and_runs():
    # The end-to-end harness binds a single `handle` overload, so the graph is
    # driven with a single input event type. The variant still spans both
    # declared member types, exercising multiplex/demultiplex codegen.
    a = DetectionEvent()
    b = MarkerEvent()
    tag = AccessTag("c")
    g = Graph()
    g.add_node("mux", Multiplex(a, b))
    g.add_node("pass", SelectAll(), upstream="mux")
    g.add_node("demux", Demultiplex(), upstream="pass")
    g.add_node("count", Count(a, tag), upstream="demux")
    g.add_node("sink", SinkAll(), upstream="count")
    ctx = ExecutionContext(tcspc.CompiledGraph(g, (a,)))
    assert ctx.access(tag).count() == 0
    ctx.handle(a.value(abstime=10, channel=0))
    ctx.handle(a.value(abstime=30, channel=0))
    assert ctx.access(tag).count() == 2
    ctx.flush()
