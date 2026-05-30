# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._access import AccessTag, _BufferAccessorSpec
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._graph import Graph
from libtcspc._param import Param

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_ProcessInBatches_event_set_preserved():
    node = tcspc.ProcessInBatches(IntEvent, 100)
    assert node._map_event_sets([(IntEvent,)]) == ((IntEvent,),)


def test_ProcessInBatches_rejects_other_events():
    node = tcspc.ProcessInBatches(IntEvent, 100)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, OtherEvent)])


def test_ProcessInBatches_codegen():
    node = tcspc.ProcessInBatches(IntEvent, 100)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::process_in_batches<int>(" in code
    assert "tcspc::arg::batch_size<std::size_t>" in code
    assert "DOWN" in code


def test_ProcessInBatches_param():
    node = tcspc.ProcessInBatches(IntEvent, Param("n"))
    assert len(node._parameters()) == 1
    assert "params.z_n" in node._cpp_expression(gencontext, DOWN)


def test_Buffer_event_set_preserved():
    node = tcspc.Buffer(IntEvent, 100, AccessTag("buf"))
    assert node._map_event_sets([(IntEvent,)]) == ((IntEvent,),)


def test_Buffer_rejects_other_events():
    node = tcspc.Buffer(IntEvent, 100, AccessTag("buf"))
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, OtherEvent)])


def test_Buffer_accesses_buffer_accessor():
    node = tcspc.Buffer(IntEvent, 100, AccessTag("buf"))
    accesses = node._accesses()
    assert len(accesses) == 1
    tag, spec = accesses[0]
    assert tag == AccessTag("buf")
    assert isinstance(spec, _BufferAccessorSpec)


def test_Buffer_codegen():
    node = tcspc.Buffer(IntEvent, 100, AccessTag("buf"))
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::buffer<int>(" in code
    assert "tcspc::arg::threshold<std::size_t>" in code
    assert 'ctx->tracker<tcspc::buffer_accessor>("buf")' in code
    assert "DOWN" in code


def test_Buffer_param():
    node = tcspc.Buffer(IntEvent, Param("n"), AccessTag("buf"))
    assert len(node._parameters()) == 1
    assert "params.z_n" in node._cpp_expression(gencontext, DOWN)


def test_RealTimeBuffer_event_set_preserved():
    node = tcspc.RealTimeBuffer(IntEvent, 100, 1000, AccessTag("buf"))
    assert node._map_event_sets([(IntEvent,)]) == ((IntEvent,),)


def test_RealTimeBuffer_rejects_other_events():
    node = tcspc.RealTimeBuffer(IntEvent, 100, 1000, AccessTag("buf"))
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, OtherEvent)])


def test_RealTimeBuffer_rejects_negative_latency():
    with pytest.raises(ValueError):
        tcspc.RealTimeBuffer(IntEvent, 100, -1, AccessTag("buf"))


def test_RealTimeBuffer_codegen():
    node = tcspc.RealTimeBuffer(IntEvent, 100, 5000, AccessTag("buf"))
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::real_time_buffer<int>(" in code
    assert "tcspc::arg::threshold<std::size_t>" in code
    assert "std::chrono::nanoseconds{5000}" in code
    assert 'ctx->tracker<tcspc::buffer_accessor>("buf")' in code
    assert "DOWN" in code


def test_RealTimeBuffer_latency_param():
    node = tcspc.RealTimeBuffer(
        IntEvent, 100, Param("latency"), AccessTag("buf")
    )
    assert len(node._parameters()) == 1
    code = node._cpp_expression(gencontext, DOWN)
    assert "std::chrono::nanoseconds{params.z_latency}" in code


def test_RealTimeBuffer_both_params():
    node = tcspc.RealTimeBuffer(
        IntEvent, Param("n"), Param("latency"), AccessTag("buf")
    )
    assert len(node._parameters()) == 2


def test_ProcessInBatches_preserves_thread_group():
    g = Graph()
    g.add_chain(
        [
            ("a", tcspc.ProcessInBatches(IntEvent, 10)),
            ("b", tcspc.ProcessInBatches(IntEvent, 10)),
        ]
    )
    port_groups, conflicts = g._thread_group_port_map()
    assert not conflicts
    assert port_groups[((), "a", "output")] == port_groups[((), "b", "output")]


def test_Buffer_emits_new_thread_group():
    g = Graph()
    g.add_chain(
        [
            ("up", tcspc.ProcessInBatches(IntEvent, 10)),
            ("buf", tcspc.Buffer(IntEvent, 10, AccessTag("buf"))),
        ]
    )
    port_groups, conflicts = g._thread_group_port_map()
    assert not conflicts
    assert (
        port_groups[((), "up", "output")] != port_groups[((), "buf", "output")]
    )


def test_RealTimeBuffer_emits_new_thread_group():
    g = Graph()
    g.add_chain(
        [
            ("up", tcspc.ProcessInBatches(IntEvent, 10)),
            (
                "buf",
                tcspc.RealTimeBuffer(IntEvent, 10, 1000, AccessTag("buf")),
            ),
        ]
    )
    port_groups, conflicts = g._thread_group_port_map()
    assert not conflicts
    assert (
        port_groups[((), "up", "output")] != port_groups[((), "buf", "output")]
    )
