# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import array
import gc
import threading
import time
from typing import final

import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc._access import AccessTag
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName, _uint8_type
from libtcspc._events import (
    BHSPCEvent,
    BinIncrementClusterEvent,
    ConstBucketEvent,
    CustomEvent,
    DetectionEvent,
    DetectionPairEvent,
    EventInstance,
    HistogramArrayProgressEvent,
    HistogramEvent,
    MarkerEvent,
    RealOneShotTimingEvent,
    SwabianTagEvent,
    TimeCorrelatedDetectionEvent,
    WarningEvent,
)
from libtcspc._execute import (
    EndOfProcessing,
    ExecutionContext,
    PySink,
    SourceHalted,
)
from libtcspc._graph import Graph
from libtcspc._numeric_traits import NumericTraits
from libtcspc._param import Param
from libtcspc._processors import (
    Append,
    Batch,
    Buffer,
    Count,
    DecodeBHSPC,
    PairOne,
    Prepend,
    RealTimeBuffer,
    SelectAll,
    SinkAll,
    SinkOnly,
    Stop,
    TimeCorrelateAtStart,
)
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))


def test_execute_graph_with_single_input():
    g = Graph()
    g.add_node("a", SinkAll())
    c = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    c.handle(123)
    c.flush()


def test_execute_cpp_to_graphviz_returns_dot_for_compiled_graph():
    g = Graph()
    g.add_node("a", SinkAll())
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    dot = ec.cpp_to_graphviz()
    assert dot.startswith("digraph G")
    assert dot.rstrip().endswith("}")
    assert "->" in dot
    assert "input_processor" in dot


def test_execute_cpp_to_graphviz_callable_before_handle():
    g = Graph()
    g.add_node("a", SinkAll())
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    assert "digraph G" in ec.cpp_to_graphviz()


def test_execute_cpp_to_graphviz_callable_after_flush():
    g = Graph()
    g.add_node("a", SinkAll())
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    ec.handle(123)
    ec.flush()
    assert "digraph G" in ec.cpp_to_graphviz()


def test_execute_node_access():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", SinkAll(), upstream="c")
    c = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    c.handle(123)
    c.flush()
    assert c.access(AccessTag("counter")).count() == 1


def test_execute_access_unknown_tag_raises():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", SinkAll(), upstream="c")
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    with pytest.raises(ValueError, match="no such access tag"):
        ec.access(AccessTag("nope"))


def test_execute_access_before_handle_returns_initial_state():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", SinkAll(), upstream="c")
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    assert ec.access(AccessTag("counter")).count() == 0


def test_execute_access_after_flush_returns_final_state():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", SinkAll(), upstream="c")
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    ec.handle(1)
    ec.handle(2)
    ec.flush()
    assert ec.access(AccessTag("counter")).count() == 2


def test_execute_access_after_end_of_processing_returns_final_state():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("s", Stop((IntEvent,), "stopped"), upstream="c")
    g.add_node("n", SinkAll(), upstream="s")
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    with pytest.raises(EndOfProcessing):
        ec.handle(42)
    assert ec.access(AccessTag("counter")).count() == 1


def test_execute_access_multiple_calls_share_state():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", SinkAll(), upstream="c")
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    ec.handle(1)
    acc1 = ec.access(AccessTag("counter"))
    ec.handle(2)
    acc2 = ec.access(AccessTag("counter"))
    assert acc1.count() == 2
    assert acc2.count() == 2


def test_execute_access_outlives_execution_context():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("counter")))
    g.add_node("a", SinkAll(), upstream="c")
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    ec.handle(1)
    ec.handle(2)
    ec.handle(3)
    acc = ec.access(AccessTag("counter"))
    del ec
    gc.collect()
    assert acc.count() == 3


def test_execute_access_with_special_character_tag_roundtrips():
    g = Graph()
    g.add_node("c", Count(IntEvent, AccessTag("foo.bar-baz/0")))
    g.add_node("a", SinkAll(), upstream="c")
    ec = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    ec.handle(1)
    ec.flush()
    assert ec.access(AccessTag("foo.bar-baz/0")).count() == 1


def test_execute_rejects_events_and_flush_when_expired():
    g = Graph()
    g.add_node("a", SinkAll())
    c = ExecutionContext(CompiledGraph(g, (IntEvent,)), {})
    c.flush()
    with pytest.raises(RuntimeError):
        c.handle(123)
    with pytest.raises(RuntimeError):
        c.flush()


def test_execute_handles_buffer_events():
    g = Graph()
    g.add_node(
        "a",
        SinkOnly(_NamedEvent(_CppTypeName("tcspc::bucket<tcspc::u8 const>"))),
    )
    c = ExecutionContext(
        CompiledGraph(g, [ConstBucketEvent(_NamedEvent(_uint8_type))]), {}
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
    g.add_node("s", SinkAll(), upstream="a")
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
    g.add_node("s", SinkAll())
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


@final
class CollectingSink(PySink):
    def __init__(self) -> None:
        self.events: list = []
        self.flushed = False

    @override
    def handle(self, event) -> None:
        self.events.append(event)

    @override
    def flush(self) -> None:
        self.flushed = True


def test_execute_emit_event_value_as_event_instance():
    g = Graph()
    g.add_node("a", Append(DetectionEvent().value(abstime=7, channel=2)))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.flush()
    assert sink.events == [DetectionEvent().value(abstime=7, channel=2)]
    assert isinstance(sink.events[0], EventInstance)


def test_execute_event_value_round_trips_through_passthrough_graph():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (DetectionEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = DetectionEvent().value(abstime=42, channel=1)
    ec.handle(sent)
    assert sink.events == [sent]
    assert isinstance(sink.events[0], EventInstance)
    assert sink.events[0] == sent


def test_execute_event_value_round_trip_preserves_field_values():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (DetectionEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(DetectionEvent().value(abstime=12345, channel=7))
    out = sink.events[0]
    assert out._fields == {"abstime": 12345, "channel": 7}


def test_execute_double_field_event_round_trips_through_passthrough_graph():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (RealOneShotTimingEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = RealOneShotTimingEvent().value(abstime=42, delay=2.5)
    ec.handle(sent)
    assert sink.events == [sent]
    assert isinstance(sink.events[0], EventInstance)
    assert sink.events[0]._fields == {"abstime": 42, "delay": 2.5}


def test_execute_detection_pair_round_trips_through_passthrough_graph():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (DetectionPairEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    start = DetectionEvent().value(abstime=0, channel=0)
    stop = DetectionEvent().value(abstime=10, channel=1)
    sent = DetectionPairEvent().value(elements=[start, stop])
    ec.handle(sent)
    assert len(sink.events) == 1
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    assert out == sent
    assert out.elements == (start, stop)
    assert len(out) == 2
    assert out[0] == start
    assert out[1] == stop
    assert isinstance(out[0], EventInstance)


def test_execute_detection_pair_emitted_by_pairing_processor():
    g = Graph()
    g.add_node(
        "p",
        PairOne(start_channel=0, stop_channels=[1], time_window=100),
    )
    cg = CompiledGraph(g, (DetectionEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(DetectionEvent().value(abstime=0, channel=0))  # start
    ec.handle(DetectionEvent().value(abstime=5, channel=1))  # stop -> pair
    ec.flush()
    pairs = [
        e
        for e in sink.events
        if isinstance(e, EventInstance)
        and type(e._event_type) is DetectionPairEvent
    ]
    assert len(pairs) == 1
    assert pairs[0].elements == (
        DetectionEvent().value(abstime=0, channel=0),
        DetectionEvent().value(abstime=5, channel=1),
    )


def test_execute_detection_pair_consumed_by_time_correlation_processor():
    g = Graph()
    g.add_node("tc", TimeCorrelateAtStart())
    cg = CompiledGraph(g, (DetectionPairEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    start = DetectionEvent().value(abstime=3, channel=0)
    stop = DetectionEvent().value(abstime=9, channel=1)
    ec.handle(DetectionPairEvent().value(elements=[start, stop]))
    ec.flush()
    assert len(sink.events) == 1
    tcd = sink.events[0]
    assert tcd.abstime == 3
    assert tcd.channel == 0
    assert tcd.difftime == 6


def test_execute_detection_pair_embedded_via_append():
    start = DetectionEvent().value(abstime=1, channel=0)
    stop = DetectionEvent().value(abstime=2, channel=1)
    pair = DetectionPairEvent().value(elements=[start, stop])
    g = Graph()
    g.add_node("a", Append(pair))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.flush()
    assert sink.events == [pair]
    assert sink.events[0].elements == (start, stop)


def test_execute_prepend_param_binds_event_at_runtime():
    g = Graph()
    g.add_node("a", Prepend(Param("evt"), event_type=DetectionEvent()))
    cg = CompiledGraph(g, (DetectionEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(
        cg, {"evt": DetectionEvent().value(abstime=99, channel=7)}, (sink,)
    )
    ec.handle(DetectionEvent().value(abstime=1, channel=2))
    ec.flush()
    assert sink.events == [
        DetectionEvent().value(abstime=99, channel=7),
        DetectionEvent().value(abstime=1, channel=2),
    ]


def test_execute_append_param_binds_event_at_flush():
    g = Graph()
    g.add_node("a", Append(Param("evt"), event_type=DetectionEvent()))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(
        cg, {"evt": DetectionEvent().value(abstime=55, channel=3)}, (sink,)
    )
    ec.flush()
    assert sink.events == [DetectionEvent().value(abstime=55, channel=3)]


def test_execute_append_param_uses_default_value():
    g = Graph()
    g.add_node(
        "a",
        Append(
            Param("evt", DetectionEvent().value(abstime=8, channel=1)),
            event_type=DetectionEvent(),
        ),
    )
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.flush()
    assert sink.events == [DetectionEvent().value(abstime=8, channel=1)]


def test_execute_append_param_rejects_wrong_type_argument():
    g = Graph()
    g.add_node("a", Append(Param("evt"), event_type=DetectionEvent()))
    cg = CompiledGraph(g)
    with pytest.raises(TypeError):
        ExecutionContext(
            cg,
            {"evt": MarkerEvent().value(abstime=1, channel=2)},
            (CollectingSink(),),
        )


def test_execute_append_param_event_with_custom_numeric_traits():
    # The event type carries non-default numeric traits; the trait struct must
    # be registered in the generated source on the Param path too.
    nt = NumericTraits(abstime_type=np.uint64)
    ev = CustomEvent("ce_append_param_traits", abstime=True, traits=nt)
    g = Graph()
    g.add_node("a", Append(Param("evt"), event_type=ev))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(cg, {"evt": ev.value(abstime=12345)}, (sink,))
    ec.flush()
    assert sink.events == [ev.value(abstime=12345)]
    assert sink.events[0]._fields == {"abstime": 12345}


def test_execute_prepend_param_binds_bucket_event_at_runtime():
    g = Graph()
    g.add_node("a", Prepend(Param("h"), event_type=HistogramEvent()))
    cg = CompiledGraph(g, (HistogramEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(
        cg, {"h": HistogramEvent().value(data_bucket=[10, 20, 30])}, (sink,)
    )
    ec.handle(HistogramEvent().value(data_bucket=[1, 1]))
    ec.flush()
    assert len(sink.events) == 2
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    arr = out.data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [10, 20, 30]
    arr1 = sink.events[1].data_bucket
    assert isinstance(arr1, np.ndarray)
    assert list(arr1) == [1, 1]


def test_execute_append_param_binds_bucket_event_at_flush():
    g = Graph()
    g.add_node("a", Append(Param("h"), event_type=HistogramEvent()))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(
        cg, {"h": HistogramEvent().value(data_bucket=[1, 2, 3, 4])}, (sink,)
    )
    ec.flush()
    assert len(sink.events) == 1
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    arr = out.data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [1, 2, 3, 4]


def test_execute_prepend_bakes_in_bucket_event_end_to_end():
    g = Graph()
    g.add_node("a", Prepend(HistogramEvent().value(data_bucket=[10, 20, 30])))
    cg = CompiledGraph(g, (HistogramEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(HistogramEvent().value(data_bucket=[1, 1]))
    ec.flush()
    assert len(sink.events) == 2
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    arr = out.data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [10, 20, 30]


def test_execute_append_bakes_in_bucket_event_end_to_end():
    g = Graph()
    g.add_node("a", Append(HistogramEvent().value(data_bucket=[1, 2, 3, 4])))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.flush()
    assert len(sink.events) == 1
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    arr = out.data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [1, 2, 3, 4]


def test_execute_string_field_event_round_trips_through_passthrough_graph():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (WarningEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = WarningEvent().value(message="something happened")
    ec.handle(sent)
    assert sink.events == [sent]
    assert isinstance(sink.events[0], EventInstance)
    assert sink.events[0]._fields == {"message": "something happened"}


def test_execute_bucket_event_value_round_trips_through_passthrough_graph():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (BinIncrementClusterEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = BinIncrementClusterEvent().value(bin_indices=[1, 2, 3])
    ec.handle(sent)
    assert len(sink.events) == 1
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    assert out == sent
    arr = out.bin_indices
    assert isinstance(arr, np.ndarray)
    assert arr.dtype == np.dtype(np.uint16)
    assert arr.flags.writeable is False


def test_execute_mixed_scalar_bucket_event_round_trips():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (HistogramArrayProgressEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = HistogramArrayProgressEvent().value(
        valid_size=3, data_bucket=[5, 6, 7, 8]
    )
    ec.handle(sent)
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    assert out == sent
    assert out.valid_size == 3
    arr = out.data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [5, 6, 7, 8]


def test_execute_empty_bucket_event_round_trips():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (BinIncrementClusterEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = BinIncrementClusterEvent().value(bin_indices=[])
    ec.handle(sent)
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    assert out == sent
    arr = out.bin_indices
    assert isinstance(arr, np.ndarray)
    assert arr.size == 0
    assert arr.dtype == np.dtype(np.uint16)


def test_execute_rejects_event_value_of_unaccepted_type():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (DetectionEvent(),))
    ec = ExecutionContext(cg, None, (CollectingSink(),))
    with pytest.raises(TypeError, match="MarkerEvent"):
        ec.handle(MarkerEvent().value(abstime=1, channel=2))


def test_execute_rejects_event_value_of_output_only_type():
    g = Graph()
    g.add_node("a", Append(MarkerEvent().value(abstime=1, channel=2)))
    cg = CompiledGraph(g, (DetectionEvent(),))
    ec = ExecutionContext(cg, None, (CollectingSink(),))
    with pytest.raises(TypeError, match="MarkerEvent"):
        ec.handle(MarkerEvent().value(abstime=1, channel=2))


def test_execute_custom_empty_event_round_trips():
    reset = CustomEvent("ce_exec_empty_roundtrip")
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (reset,))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = reset.value()
    ec.handle(sent)
    assert sink.events == [sent]
    assert isinstance(sink.events[0], EventInstance)


def test_execute_custom_abstime_event_round_trips():
    px = CustomEvent(
        "ce_exec_abstime_roundtrip", abstime=True, traits=NumericTraits()
    )
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (px,))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = px.value(abstime=12345)
    ec.handle(sent)
    assert sink.events == [sent]
    assert sink.events[0]._fields == {"abstime": 12345}


def test_execute_prepend_custom_empty_event():
    reset = CustomEvent("ce_exec_prepend")
    g = Graph()
    g.add_node("a", Prepend(reset.value()))
    cg = CompiledGraph(g, (reset,))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(reset.value())
    ec.flush()
    assert sink.events == [reset.value(), reset.value()]


def test_execute_append_custom_abstime_event():
    nt = NumericTraits()
    marker = CustomEvent("ce_exec_append", abstime=True, traits=nt)
    g = Graph()
    g.add_node("a", Append(marker.value(abstime=99)))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.flush()
    assert sink.events == [marker.value(abstime=99)]


def test_execute_same_name_custom_events_distinct_shapes_coexist():
    # Two CustomEvents sharing a name but differing in shape must coexist in one
    # process (content-addressed types; no conflict guard) and each round-trips.
    empty = CustomEvent("ce_coexist")
    abst = CustomEvent("ce_coexist", abstime=True, traits=NumericTraits())
    assert empty != abst

    g1 = Graph()
    g1.add_node("a", SelectAll())
    ec1 = ExecutionContext(
        CompiledGraph(g1, (empty,)), None, (sink1 := CollectingSink(),)
    )
    ec1.handle(empty.value())
    assert sink1.events == [empty.value()]

    g2 = Graph()
    g2.add_node("a", SelectAll())
    ec2 = ExecutionContext(
        CompiledGraph(g2, (abst,)), None, (sink2 := CollectingSink(),)
    )
    ec2.handle(abst.value(abstime=5))
    assert sink2.events == [abst.value(abstime=5)]


def test_execute_pass_through_integers():
    g = Graph()
    g.add_node("b", Batch(_NamedEvent(_CppTypeName("int")), batch_size=1))
    cg = CompiledGraph(g, (_NamedEvent(_CppTypeName("int")),))

    log: list[str] = []
    sink = MockSink(log)
    c = ExecutionContext(cg, None, (sink,))
    c.handle(42)
    assert log == ["handle([42])"]
    c.flush()
    assert log == ["handle([42])", "flush()"]


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
    g.add_node("b", Batch(_NamedEvent(_CppTypeName("int")), batch_size=1))
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
    g.add_node("s", SinkAll(), upstream="a")
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
    g.add_node("s", SinkAll(), upstream="b")
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
    g.add_node("s", SinkAll())
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
    g.add_node("s", SinkAll())
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
    g.add_node("s", SinkAll(), upstream="b")
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


def test_execute_Buffer_pumps_all_events_in_order():
    g = Graph()
    g.add_node("b", Buffer(DetectionEvent(), 1, AccessTag("buf")))
    sink = CollectingSink()
    ec = ExecutionContext(CompiledGraph(g, (DetectionEvent(),)), None, (sink,))
    buf = ec.access(AccessTag("buf"))

    errors: list = []

    def pump():
        try:
            buf.pump()
        except (EndOfProcessing, SourceHalted):
            pass
        except BaseException as e:  # noqa: BLE001
            errors.append(e)

    t = threading.Thread(target=pump)
    t.start()
    sent = [DetectionEvent().value(abstime=i, channel=0) for i in range(20)]
    for e in sent:
        ec.handle(e)
    ec.flush()
    t.join()

    assert not errors
    assert sink.events == sent
    assert sink.flushed


def test_execute_Buffer_halt_before_flush_raises_SourceHalted():
    g = Graph()
    g.add_node("b", Buffer(DetectionEvent(), 1, AccessTag("buf")))
    sink = CollectingSink()
    ec = ExecutionContext(CompiledGraph(g, (DetectionEvent(),)), None, (sink,))
    buf = ec.access(AccessTag("buf"))

    outcome: list = []

    def pump():
        try:
            buf.pump()
            outcome.append(("returned", None))
        except SourceHalted as e:
            outcome.append(("source_halted", e))
        except BaseException as e:  # noqa: BLE001
            outcome.append(("other", e))

    t = threading.Thread(target=pump)
    t.start()
    buf.halt()
    t.join()

    assert outcome == [("source_halted", outcome[0][1])]
    assert not sink.flushed


def test_execute_Buffer_downstream_error_surfaces_on_both_threads():
    @final
    class RaisingSink(PySink):
        @override
        def handle(self, event) -> None:
            raise RuntimeError("boom")

        @override
        def flush(self) -> None:
            pass

    g = Graph()
    g.add_node("b", Buffer(DetectionEvent(), 1, AccessTag("buf")))
    ec = ExecutionContext(
        CompiledGraph(g, (DetectionEvent(),)), None, (RaisingSink(),)
    )
    buf = ec.access(AccessTag("buf"))

    pump_error: list = []

    def pump():
        try:
            buf.pump()
        except BaseException as e:  # noqa: BLE001
            pump_error.append(e)

    t = threading.Thread(target=pump)
    t.start()

    # The downstream sink raises while pumping; the buffer records the
    # termination and the producer's next handle()/flush() raises
    # EndOfProcessing.
    with pytest.raises(EndOfProcessing):
        for i in range(1000):
            ec.handle(DetectionEvent().value(abstime=i, channel=0))
            time.sleep(0.001)

    t.join()
    assert len(pump_error) == 1
    assert isinstance(pump_error[0], RuntimeError)


def test_execute_RealTimeBuffer_emits_after_latency():
    g = Graph()
    # Large threshold so emission can only be triggered by the latency limit.
    g.add_node(
        "b",
        RealTimeBuffer(
            DetectionEvent(), 1_000_000, 20_000_000, AccessTag("buf")
        ),
    )
    sink = CollectingSink()
    ec = ExecutionContext(CompiledGraph(g, (DetectionEvent(),)), None, (sink,))
    buf = ec.access(AccessTag("buf"))

    errors: list = []

    def pump():
        try:
            buf.pump()
        except (EndOfProcessing, SourceHalted):
            pass
        except BaseException as e:  # noqa: BLE001
            errors.append(e)

    t = threading.Thread(target=pump)
    t.start()
    sent = DetectionEvent().value(abstime=1, channel=0)
    ec.handle(sent)

    deadline = time.monotonic() + 10.0
    while not sink.events and time.monotonic() < deadline:
        time.sleep(0.01)

    assert sink.events == [sent]

    ec.flush()
    t.join()
    assert not errors


def test_execute_raw_device_event_round_trips_through_passthrough_graph():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (BHSPCEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    sent = BHSPCEvent().value(bytes=b"\x12\x34\x56\x78")
    ec.handle(sent)
    assert sink.events == [sent]
    out = sink.events[0]
    assert isinstance(out, EventInstance)
    assert out.bytes == b"\x12\x34\x56\x78"
    assert isinstance(out.bytes, bytes)


def test_execute_raw_device_event_16byte_round_trips():
    g = Graph()
    g.add_node("a", SelectAll())
    cg = CompiledGraph(g, (SwabianTagEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    raw = bytes(range(16))
    ec.handle(SwabianTagEvent().value(bytes=raw))
    assert sink.events[0].bytes == raw


def test_execute_emit_raw_device_event_value_via_append():
    g = Graph()
    g.add_node("a", Append(BHSPCEvent().value(bytes=b"\x12\x34\x56\x78")))
    cg = CompiledGraph(g)
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.flush()
    assert sink.events == [BHSPCEvent().value(bytes=b"\x12\x34\x56\x78")]
    assert sink.events[0].bytes == b"\x12\x34\x56\x78"


def test_execute_emit_raw_device_event_value_via_prepend():
    g = Graph()
    g.add_node("a", Prepend(BHSPCEvent().value(bytes=b"\xde\xad\xbe\xef")))
    cg = CompiledGraph(g, (BHSPCEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(BHSPCEvent().value(bytes=b"\x01\x02\x03\x04"))
    ec.flush()
    assert sink.events == [
        BHSPCEvent().value(bytes=b"\xde\xad\xbe\xef"),
        BHSPCEvent().value(bytes=b"\x01\x02\x03\x04"),
    ]


def test_execute_raw_device_event_decoded_by_decode_processor():
    # An all-zero BH SPC record decodes to a single valid photon at
    # abstime=0, channel=0, difftime=0.
    g = Graph()
    g.add_node("decode", DecodeBHSPC())
    cg = CompiledGraph(g, (BHSPCEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(BHSPCEvent().value(bytes=b"\x00\x00\x00\x00"))
    ec.flush()
    detections = [
        e
        for e in sink.events
        if isinstance(e, EventInstance)
        and type(e._event_type) is TimeCorrelatedDetectionEvent
    ]
    assert len(detections) == 1
    assert detections[0]._fields == {
        "abstime": 0,
        "channel": 0,
        "difftime": 0,
    }


def test_execute_raw_device_event_decoded_nontrivial_record():
    # macrotime=0x123, routing=5, adc=0x456 (see bh_spc.hpp bit layout).
    g = Graph()
    g.add_node("decode", DecodeBHSPC())
    cg = CompiledGraph(g, (BHSPCEvent(),))
    sink = CollectingSink()
    ec = ExecutionContext(cg, None, (sink,))
    ec.handle(BHSPCEvent().value(bytes=b"\x23\x51\x56\x04"))
    ec.flush()
    detections = [
        e
        for e in sink.events
        if isinstance(e, EventInstance)
        and type(e._event_type) is TimeCorrelatedDetectionEvent
    ]
    assert len(detections) == 1
    assert detections[0]._fields == {
        "abstime": 0x123,
        "channel": 5,
        "difftime": 0x456,
    }
