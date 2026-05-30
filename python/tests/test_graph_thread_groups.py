# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence

import pytest
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression
from libtcspc._events import EventType
from libtcspc._graph import Graph, Subgraph, _ThreadColoring, _ThreadGroup
from libtcspc._node import Node, _RelayNode
from typing_extensions import override

# Concrete test nodes (not mocks) so they survive the deepcopy in Subgraph.


class _Relay(_RelayNode):
    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        raise NotImplementedError


class _BufferLike(_Relay):
    @override
    def _map_thread_groups(
        self,
        input_groups: Sequence[_ThreadGroup],
        coloring: "_ThreadColoring",
    ) -> tuple[_ThreadGroup, ...]:
        return (coloring.mint(),)


class _Fan(Node):
    # Single input, N outputs (named output-0..).
    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        return tuple(() for _ in self.outputs())

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        raise NotImplementedError


class _Join(Node):
    # N inputs, single output, default (safe-by-default) thread join.
    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        return ((),)

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        raise NotImplementedError


class _OptOutJoin(_Join):
    @override
    def _map_thread_groups(
        self,
        input_groups: Sequence[_ThreadGroup],
        coloring: "_ThreadColoring",
    ) -> tuple[_ThreadGroup, ...]:
        # Tolerate multiple input threads: do not report a conflict.
        return (input_groups[0],)


def _fan2() -> _Fan:
    return _Fan(input=["input"], output=["output-0", "output-1"])


def _join2() -> _Join:
    return _Join(input=["input-0", "input-1"], output=["output"])


def test_linear_chain_single_group():
    g = Graph()
    g.add_chain([("a", _Relay()), ("b", _Relay()), ("c", _Relay())])
    g._check_thread_safety()  # No raise.
    port_groups, conflicts = g._thread_group_port_map()
    assert not conflicts
    assert len(set(port_groups.values())) == 1


def test_buffer_mints_new_group():
    g = Graph()
    g.add_chain([("a", _Relay()), ("buf", _BufferLike()), ("c", _Relay())])
    port_groups, conflicts = g._thread_group_port_map()
    assert not conflicts
    assert (
        port_groups[((), "a", "output")] != port_groups[((), "buf", "output")]
    )
    assert (
        port_groups[((), "buf", "output")] == port_groups[((), "c", "output")]
    )


def test_broadcast_to_join_passes():
    g = Graph()
    g.add_node("fan", _fan2())
    g.add_node("join", _join2())
    g.connect(("fan", "output-0"), ("join", "input-0"))
    g.connect(("fan", "output-1"), ("join", "input-1"))
    g._check_thread_safety()  # No raise.
    _, conflicts = g._thread_group_port_map()
    assert not conflicts


def test_join_across_buffer_boundary_raises():
    g = Graph()
    g.add_node("fan", _fan2())
    g.add_node("buf", _BufferLike(), upstream=("fan", "output-0"))
    g.add_node("join", _join2())
    g.connect(("buf", "output"), ("join", "input-0"))
    g.connect(("fan", "output-1"), ("join", "input-1"))
    with pytest.raises(ValueError, match="join"):
        g._check_thread_safety()


def test_join_conflict_recorded_non_strict():
    g = Graph()
    g.add_node("fan", _fan2())
    g.add_node("buf", _BufferLike(), upstream=("fan", "output-0"))
    g.add_node("join", _join2())
    g.connect(("buf", "output"), ("join", "input-0"))
    g.connect(("fan", "output-1"), ("join", "input-1"))
    _, conflicts = g._thread_group_port_map()
    assert len(conflicts) == 1
    qual_name, bad_ports = conflicts[0]
    assert qual_name == "join"
    assert set(bad_ports) <= {"input-0", "input-1"}


def test_join_opt_out_does_not_raise():
    g = Graph()
    g.add_node("fan", _fan2())
    g.add_node("buf", _BufferLike(), upstream=("fan", "output-0"))
    join = _OptOutJoin(input=["input-0", "input-1"], output=["output"])
    g.add_node("join", join)
    g.connect(("buf", "output"), ("join", "input-0"))
    g.connect(("fan", "output-1"), ("join", "input-1"))
    g._check_thread_safety()  # No raise.


def test_subgraph_inner_conflict_reported_with_qualified_name():
    inner = Graph()
    inner.add_node("fan", _fan2())
    inner.add_node("buf", _BufferLike(), upstream=("fan", "output-0"))
    inner.add_node("join", _join2())
    inner.connect(("buf", "output"), ("join", "input-0"))
    inner.connect(("fan", "output-1"), ("join", "input-1"))

    outer = Graph()
    outer.add_node("sg", Subgraph(inner))
    with pytest.raises(ValueError, match="sg/join"):
        outer._check_thread_safety()
