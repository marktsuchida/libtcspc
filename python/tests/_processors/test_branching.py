# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import final

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import Broadcast, Route
from libtcspc._routers import ChannelRouter, NullRouter
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


@final
class MockSink(PySink):
    def __init__(self, log: list[str]) -> None:
        self._log = log

    @override
    def handle(self, event) -> None:
        self._log.append(f"handle({event})")

    @override
    def flush(self) -> None:
        self._log.append("flush()")


def test_Broadcast_port_naming_int_form():
    node = Broadcast(IntEvent, outputs=3)
    assert node.outputs() == ("output-0", "output-1", "output-2")


def test_Broadcast_port_naming_sequence_form():
    node = Broadcast(IntEvent, outputs=("a", "b"))
    assert node.outputs() == ("a", "b")


def test_Broadcast_has_single_input_port():
    node = Broadcast(IntEvent, outputs=2)
    assert node.inputs() == ("input",)


def test_Broadcast_rejects_zero_outputs():
    with pytest.raises(ValueError):
        Broadcast(IntEvent, outputs=0)
    with pytest.raises(ValueError):
        Broadcast(IntEvent, outputs=())


def test_Broadcast_rejects_duplicate_output_names():
    with pytest.raises(ValueError):
        Broadcast(IntEvent, outputs=("a", "a"))


def test_Broadcast_map_event_sets_returns_event_types_per_output():
    node = Broadcast(IntEvent, OtherEvent, outputs=2)
    result = node._map_event_sets([(IntEvent,)])
    assert result == ((IntEvent, OtherEvent), (IntEvent, OtherEvent))


def test_Broadcast_map_event_sets_rejects_wrong_number_of_inputs():
    node = Broadcast(IntEvent, outputs=2)
    with pytest.raises(ValueError):
        node._map_event_sets([])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,), (IntEvent,)])


def test_Broadcast_map_event_sets_rejects_unconfigured_event():
    node = Broadcast(IntEvent, outputs=2)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, OtherEvent)])


def test_Broadcast_codegen_calls_tcspc_broadcast():
    node = Broadcast(IntEvent, OtherEvent, outputs=2)
    code = node._cpp_expression(
        gencontext, [_CppExpression("d0"), _CppExpression("d1")]
    )
    assert "tcspc::broadcast<tcspc::type_list<int, long>>(d0, d1)" in code


def test_Broadcast_codegen_rejects_wrong_number_of_downstreams():
    node = Broadcast(IntEvent, outputs=2)
    with pytest.raises(ValueError):
        node._cpp_expression(gencontext, [_CppExpression("d0")])


def test_Broadcast_end_to_end_delivers_to_all_outputs():
    int_event = _NamedEvent(_CppTypeName("int"))
    g = Graph()
    g.add_node("bc", Broadcast(int_event, outputs=2))
    # Both of bc's output ports are unconnected, so they become the graph's
    # two output ports -> two sinks.
    cg = CompiledGraph(g, (int_event,))

    log0: list[str] = []
    log1: list[str] = []
    c = ExecutionContext(cg, None, (MockSink(log0), MockSink(log1)))
    c.handle(42)
    assert log0 == ["handle(42)"]
    assert log1 == ["handle(42)"]
    c.flush()
    assert log0 == ["handle(42)", "flush()"]
    assert log1 == ["handle(42)", "flush()"]


def test_Route_port_naming_int_form():
    node = Route(IntEvent, router=NullRouter(), outputs=3)
    assert node.outputs() == ("output-0", "output-1", "output-2")


def test_Route_port_naming_sequence_form():
    node = Route(IntEvent, router=NullRouter(), outputs=("a", "b"))
    assert node.outputs() == ("a", "b")


def test_Route_has_single_input_port():
    node = Route(IntEvent, router=NullRouter(), outputs=2)
    assert node.inputs() == ("input",)


def test_Route_rejects_zero_outputs():
    with pytest.raises(ValueError):
        Route(IntEvent, router=NullRouter(), outputs=0)
    with pytest.raises(ValueError):
        Route(IntEvent, router=NullRouter(), outputs=())


def test_Route_rejects_duplicate_output_names():
    with pytest.raises(ValueError):
        Route(IntEvent, router=NullRouter(), outputs=("a", "a"))


def test_Route_rejects_routed_broadcast_overlap():
    with pytest.raises(ValueError):
        Route(
            IntEvent,
            broadcast_event_types=(IntEvent,),
            router=NullRouter(),
            outputs=1,
        )


def test_Route_map_event_sets_returns_allowed_per_output():
    node = Route(
        IntEvent,
        broadcast_event_types=(OtherEvent,),
        router=NullRouter(),
        outputs=2,
    )
    result = node._map_event_sets([(IntEvent, OtherEvent)])
    assert result == ((IntEvent, OtherEvent), (IntEvent, OtherEvent))


def test_Route_map_event_sets_rejects_wrong_number_of_inputs():
    node = Route(IntEvent, router=NullRouter(), outputs=2)
    with pytest.raises(ValueError):
        node._map_event_sets([])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,), (IntEvent,)])


def test_Route_map_event_sets_rejects_unconfigured_event():
    node = Route(IntEvent, router=NullRouter(), outputs=2)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, OtherEvent)])


def test_Route_parameters_forward_router():
    p: Param = Param("ci")
    node = Route(IntEvent, router=ChannelRouter(p, arity=2), outputs=2)
    params = node._parameters()
    assert len(params) == 1
    assert params[0][0] is p
    encoders = node._param_encoders()
    assert set(encoders.keys()) == {"ci"}
    assert encoders["ci"]({0: 1, 1: 0}) == [(0, 1), (1, 0)]


def test_Route_parameters_empty_for_parameterless_router():
    node = Route(IntEvent, router=NullRouter(), outputs=2)
    assert len(node._parameters()) == 0
    assert len(node._param_encoders()) == 0


def test_Route_codegen_calls_tcspc_route_with_router_first():
    node = Route(
        IntEvent,
        broadcast_event_types=(OtherEvent,),
        router=NullRouter(),
        outputs=2,
    )
    code = node._cpp_expression(
        gencontext, [_CppExpression("d0"), _CppExpression("d1")]
    )
    assert (
        "tcspc::route<tcspc::type_list<int>, tcspc::type_list<long>>("
        "tcspc::null_router(), d0, d1)" in code
    )


def test_Route_codegen_rejects_wrong_number_of_downstreams():
    node = Route(IntEvent, router=NullRouter(), outputs=2)
    with pytest.raises(ValueError):
        node._cpp_expression(gencontext, [_CppExpression("d0")])


def test_Route_end_to_end_broadcasts_to_all_outputs():
    # With no routed event types, every event is broadcast to all downstreams.
    int_event = _NamedEvent(_CppTypeName("int"))
    g = Graph()
    g.add_node(
        "rt",
        Route(
            broadcast_event_types=(int_event,),
            router=NullRouter(),
            outputs=2,
        ),
    )
    cg = CompiledGraph(g, (int_event,))

    log0: list[str] = []
    log1: list[str] = []
    c = ExecutionContext(cg, None, (MockSink(log0), MockSink(log1)))
    c.handle(42)
    assert log0 == ["handle(42)"]
    assert log1 == ["handle(42)"]
    c.flush()
    assert log0 == ["handle(42)", "flush()"]
    assert log1 == ["handle(42)", "flush()"]


def _channel_routing_graph(router: ChannelRouter) -> Graph:
    import libtcspc as tcspc

    dt = tcspc.NumericTraits()
    g = Graph()
    g.add_node("dec", tcspc.DecodeBHSPC(dt))
    g.add_node(
        "rt",
        Route(
            tcspc.TimeCorrelatedDetectionEvent(dt),
            broadcast_event_types=(
                tcspc.DataLostEvent(dt),
                tcspc.MarkerEvent(dt),
                tcspc.TimeReachedEvent(dt),
                tcspc.WarningEvent(),
            ),
            router=router,
            outputs=2,
        ),
        upstream="dec",
    )
    g.add_node("s0", tcspc.SinkAll())
    g.add_node("s1", tcspc.SinkAll())
    g.connect(("rt", "output-0"), "s0")
    g.connect(("rt", "output-1"), "s1")
    return g


def test_Route_with_channel_router_compiles_end_to_end():
    import libtcspc as tcspc

    g = _channel_routing_graph(ChannelRouter({0: 0, 1: 1}))
    cg = CompiledGraph(g, (tcspc.BHSPCEvent(),))
    ExecutionContext(cg).flush()


def test_Route_with_param_channel_indices_binds_dict_at_execution():
    import libtcspc as tcspc

    g = _channel_routing_graph(ChannelRouter(Param("ci"), arity=2))
    cg = CompiledGraph(g, (tcspc.BHSPCEvent(),))

    # The dict is accepted (encoded to a list of pairs and bound to the
    # std::array<std::pair<...>, 2> param field) at execution time.
    ExecutionContext(cg, {"ci": {0: 0, 1: 1}}).flush()


def test_Route_with_param_channel_indices_rejects_wrong_size_dict():
    import libtcspc as tcspc

    g = _channel_routing_graph(ChannelRouter(Param("ci"), arity=2))
    cg = CompiledGraph(g, (tcspc.BHSPCEvent(),))

    # arity is 2, so a one-entry dict cannot fill the fixed-size array.
    with pytest.raises(Exception):  # noqa: B017
        ExecutionContext(cg, {"ci": {0: 0}})
