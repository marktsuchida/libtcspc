# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import array
from typing import final

import pytest
from _test_helpers import _NamedEvent
from libtcspc._access import AccessTag
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName, _uint8_type
from libtcspc._events import BucketEvent
from libtcspc._execute import EndOfProcessing, ExecutionContext, PySink
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import (
    Batch,
    Count,
    NullSink,
    SelectAll,
    SinkEvents,
    Stop,
)
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))


def test_execute_graph_with_single_input():
    g = Graph()
    g.add_node("a", NullSink())
    c = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    c.handle(123)
    c.flush()


def test_execute_node_access():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", NullSink(), upstream="c")
    c = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    c.handle(123)
    c.flush()
    assert c.access(AccessTag("counter")).count() == 1


def test_execute_rejects_events_and_flush_when_expired():
    g = Graph()
    g.add_node("a", NullSink())
    c = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    c.flush()
    with pytest.raises(RuntimeError):
        c.handle(123)
    with pytest.raises(RuntimeError):
        c.flush()


def test_execute_handles_buffer_events():
    g = Graph()
    g.add_node(
        "a", SinkEvents(_NamedEvent(_CppTypeName("tcspc::bucket<tcspc::u8>")))
    )
    c = ExecutionContext(
        CompiledGraph(g, [BucketEvent(_NamedEvent(_uint8_type))]), {}
    )
    c.handle(b"")
    c.handle(b"abc")
    c.handle(memoryview(b""))
    c.handle(array.array("B", [1, 2, 3]))
    with pytest.raises(TypeError):
        c.handle(array.array("I", [1, 2, 3]))


def test_execute_require_parameter_with_no_default():
    g = Graph()
    g.add_node("a", Stop((), Param("a_msg")))
    g.add_node("s", NullSink(), upstream="a")
    cg = CompiledGraph(g)
    ExecutionContext(cg, {_CppIdentifier("a_msg"): "hello"})
    with pytest.raises(ValueError):
        ExecutionContext(cg)
    with pytest.raises(ValueError):
        ExecutionContext(cg, {})
    with pytest.raises(TypeError):
        ExecutionContext(cg, {_CppIdentifier("a_msg"): 123})


def test_execute_unknown_parameter():
    g = Graph()
    g.add_node("s", NullSink())
    cg = CompiledGraph(g)
    ExecutionContext(cg)
    with pytest.raises(ValueError):
        ExecutionContext(cg, {_CppIdentifier("blah"): "hello"})


@final
class MockSink(PySink):
    def __init__(self, log) -> None:
        self._log = log

    @override
    def handle(self, event) -> None:
        self._log.append(f"handle({event})")

    @override
    def flush(self) -> None:
        self._log.append("flush()")


def test_execute_pass_through_integers():
    g = Graph()
    g.add_node("r", SelectAll())
    cg = CompiledGraph(g, (_NamedEvent(_CppTypeName("int")),))

    log: list[str] = []
    sink = MockSink(log)
    c = ExecutionContext(cg, None, (sink,))
    c.handle(42)
    assert log == ["handle(42)"]
    c.flush()
    assert log == ["handle(42)", "flush()"]


def test_execute_emit_bucket():
    g = Graph()
    g.add_node("b", Batch(_NamedEvent(_CppTypeName("int")), batch_size=2))
    cg = CompiledGraph(g, (_NamedEvent(_CppTypeName("int")),))

    log: list[str] = []
    sink = MockSink(log)
    c = ExecutionContext(cg, None, (sink,))
    c.handle(42)
    assert len(log) == 0
    c.handle(43)
    assert log == ["handle([42 43])"]


def test_execute_sink_exception_propagates():
    g = Graph()
    g.add_node("r", SelectAll())
    cg = CompiledGraph(g, (_NamedEvent(_CppTypeName("int")),))

    @final
    class RaisingSink(PySink):
        @override
        def handle(self, event) -> None:
            raise IndexError("test exception")

        @override
        def flush(self) -> None:
            pass

    sink = RaisingSink()
    c = ExecutionContext(cg, None, (sink,))
    with pytest.raises(IndexError):
        c.handle(42)


def _stop_graph_with_param(param: Param[str]) -> Graph:
    g = Graph()
    g.add_node("a", Stop((IntEvent,), param))
    g.add_node("s", NullSink(), upstream="a")
    return g


def test_execute_uses_default_when_argument_missing():
    cg = CompiledGraph(_stop_graph_with_param(Param("p", "default-msg")))
    ExecutionContext(cg)


def test_execute_uses_default_when_arguments_dict_omits_key():
    # Mix of supplied and unsupplied (defaulted) params — verify only the
    # missing-key path falls back while the supplied one is honored.
    g = Graph()
    g.add_node("a", Stop((IntEvent,), Param("supplied", "default-1")))
    g.add_node(
        "b", Stop((IntEvent,), Param("defaulted", "default-2")), upstream="a"
    )
    g.add_node("s", NullSink(), upstream="b")
    cg = CompiledGraph(g, (IntEvent,))
    c = ExecutionContext(cg, {"supplied": "given-1"})
    with pytest.raises(EndOfProcessing) as exc_info:
        c.handle(42)
    assert "given-1" in exc_info.value.args[0]


def test_execute_argument_overrides_default():
    cg = CompiledGraph(
        _stop_graph_with_param(Param("p", "default-msg")), (IntEvent,)
    )
    c = ExecutionContext(cg, {"p": "supplied-msg"})
    with pytest.raises(EndOfProcessing) as exc_info:
        c.handle(42)
    assert "supplied-msg" in exc_info.value.args[0]


def test_execute_missing_required_param_message():
    cg = CompiledGraph(_stop_graph_with_param(Param("required")))
    with pytest.raises(ValueError, match="required"):
        ExecutionContext(cg)


def test_execute_unknown_argument_message():
    g = Graph()
    g.add_node("s", NullSink())
    cg = CompiledGraph(g)
    with pytest.raises(ValueError, match="bogus"):
        ExecutionContext(cg, {"bogus": "value"})


def test_execute_arguments_dict_not_mutated():
    cg = CompiledGraph(_stop_graph_with_param(Param("p")))
    args = {"p": "supplied"}
    ExecutionContext(cg, args)
    assert args == {"p": "supplied"}


def test_execute_arguments_none_with_no_params_ok():
    g = Graph()
    g.add_node("s", NullSink())
    cg = CompiledGraph(g)
    ExecutionContext(cg, None)


def test_execute_arguments_none_with_required_param_raises():
    cg = CompiledGraph(_stop_graph_with_param(Param("p")))
    with pytest.raises(ValueError):
        ExecutionContext(cg, None)


def test_execute_param_name_with_special_chars():
    g = Graph()
    g.add_node(
        "b",
        Batch(
            _NamedEvent(_CppTypeName("int")), batch_size=Param("batch-size", 2)
        ),
    )
    cg = CompiledGraph(g, (_NamedEvent(_CppTypeName("int")),))
    log: list[str] = []
    sink = MockSink(log)
    c = ExecutionContext(cg, None, (sink,))
    c.handle(10)
    assert len(log) == 0
    c.handle(20)
    assert log == ["handle([10 20])"]


def test_execute_argument_key_uses_param_name_not_identifier():
    g = Graph()
    g.add_node(
        "b",
        Batch(
            _NamedEvent(_CppTypeName("int")), batch_size=Param("batch-size", 4)
        ),
    )
    cg = CompiledGraph(g, (_NamedEvent(_CppTypeName("int")),))
    # Raw param name is the public key.
    ExecutionContext(cg, {"batch-size": 2}, (MockSink([]),))
    # The mangled C++ identifier is *not* accepted; with the default supplying
    # the param, the leftover mangled-identifier key surfaces as the unknown
    # argument error.
    with pytest.raises(ValueError, match="z_batchQ2dsize"):
        ExecutionContext(cg, {"z_batchQ2dsize": 2}, (MockSink([]),))


def test_execute_two_params_independent():
    g = Graph()
    g.add_node("a", Stop((IntEvent,), Param("p1", "default-1")))
    g.add_node("b", Stop((IntEvent,), Param("p2", "default-2")), upstream="a")
    g.add_node("s", NullSink(), upstream="b")
    cg = CompiledGraph(g, (IntEvent,))

    # Override p1 only; p2 keeps its default.
    c = ExecutionContext(cg, {"p1": "supplied-1"})
    with pytest.raises(EndOfProcessing) as exc_info:
        c.handle(42)
    assert "supplied-1" in exc_info.value.args[0]

    # Override p2 only; p1 keeps its default.
    c = ExecutionContext(cg, {"p2": "supplied-2"})
    with pytest.raises(EndOfProcessing) as exc_info:
        c.handle(42)
    assert "default-1" in exc_info.value.args[0]
