# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._access import AccessTag
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._execute import ExecutionContext
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import Append, Count, Prepend, SinkAll, SourceNothing

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_SinkAll_has_no_output_ports():
    node = SinkAll()
    assert node.outputs() == ()


def test_SinkAll_accepts_any_input_event_set():
    node = SinkAll()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == ()
    assert node._map_event_sets([()]) == ()


def test_SinkAll_codegen_is_tcspc_sink_all():
    node = SinkAll()
    assert node._cpp_expression(gencontext, []) == "tcspc::sink_all()"


def test_SourceNothing_rejects_nonempty_input_set():
    node = SourceNothing()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_SourceNothing_output_event_set_is_empty():
    node = SourceNothing()
    assert node._map_event_sets([()]) == ((),)


def test_SourceNothing_codegen_calls_tcspc_source_nothing():
    node = SourceNothing()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::source_nothing(" in code
    assert "DOWN" in code


def test_Prepend_event_set_adds_inserted_event():
    node = Prepend(tcspc.DetectionEvent().value(abstime=0, channel=0))
    out = node._relay_map_event_set((IntEvent,))
    cpp = [e._cpp_type_name() for e in out]
    assert IntEvent._cpp_type_name() in cpp
    assert tcspc.DetectionEvent()._cpp_type_name() in cpp


def test_Prepend_event_set_does_not_duplicate_inserted_event():
    det = tcspc.DetectionEvent()
    node = Prepend(det.value(abstime=0, channel=0))
    out = node._relay_map_event_set((det,))
    assert (
        sum(1 for e in out if e._cpp_type_name() == det._cpp_type_name()) == 1
    )


def test_Prepend_codegen():
    node = Prepend(tcspc.DetectionEvent().value(abstime=5, channel=3))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::prepend(")
    assert ".abstime = static_cast<" in code
    assert "(5)" in code
    assert ".channel = static_cast<" in code
    assert "(3)" in code
    assert "DOWN" in code


def test_Prepend_bakes_in_bucket_carrying_event():
    node = Prepend(tcspc.HistogramEvent().value(data_bucket=[10, 20, 30]))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::prepend(")
    assert "make_owning_bucket<" in code
    assert "{" in code and "}" in code
    assert "DOWN" in code


def test_Prepend_param_requires_event_type():
    with pytest.raises(ValueError, match="event_type is required"):
        Prepend(Param("evt"))


def test_Prepend_param_accepts_bucket_event_type():
    node = Prepend(Param("h"), event_type=tcspc.HistogramEvent())
    params = node._parameters()
    assert len(params) == 1
    assert params[0][0].name == "h"
    assert params[0][1] == tcspc.HistogramEvent()._cpp_type_name()
    assert node._value_event_types() == (tcspc.HistogramEvent(),)
    assert "h" in node._param_encoders()


def test_Prepend_param_rejects_array_event_type():
    with pytest.raises(TypeError, match="not yet supported"):
        Prepend(Param("evt"), event_type=tcspc.DetectionPairEvent())


def test_Prepend_concrete_rejects_mismatched_event_type():
    with pytest.raises(ValueError, match="event_type does not match"):
        Prepend(
            tcspc.DetectionEvent().value(abstime=0, channel=0),
            event_type=tcspc.MarkerEvent(),
        )


def test_Prepend_concrete_accepts_matching_event_type():
    Prepend(
        tcspc.DetectionEvent().value(abstime=0, channel=0),
        event_type=tcspc.DetectionEvent(),
    )


def test_Prepend_param_codegen_references_params_struct():
    node = Prepend(Param("evt"), event_type=tcspc.DetectionEvent())
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::prepend(")
    assert "params.z_evt" in code
    assert "DOWN" in code


def test_Prepend_param_declares_parameter():
    node = Prepend(Param("evt"), event_type=tcspc.DetectionEvent())
    params = node._parameters()
    assert len(params) == 1
    assert params[0][0].name == "evt"


def test_Prepend_param_default_value_must_match_type():
    with pytest.raises(ValueError, match="default_value"):
        Prepend(
            Param("evt", tcspc.MarkerEvent().value(abstime=0, channel=0)),
            event_type=tcspc.DetectionEvent(),
        )


def test_Append_event_set_adds_inserted_event():
    node = Append(tcspc.DetectionEvent().value(abstime=0, channel=0))
    out = node._relay_map_event_set((IntEvent,))
    cpp = [e._cpp_type_name() for e in out]
    assert IntEvent._cpp_type_name() in cpp
    assert tcspc.DetectionEvent()._cpp_type_name() in cpp


def test_Append_event_set_from_empty_input():
    node = Append(tcspc.DetectionEvent().value(abstime=0, channel=0))
    out = node._relay_map_event_set(())
    cpp = [e._cpp_type_name() for e in out]
    assert cpp == [tcspc.DetectionEvent()._cpp_type_name()]


def test_Append_bakes_in_bucket_carrying_event():
    node = Append(tcspc.HistogramEvent().value(data_bucket=[1, 2, 3]))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::append(")
    assert "make_owning_bucket<" in code
    assert "{" in code and "}" in code
    assert "DOWN" in code


def test_Append_codegen():
    node = Append(tcspc.DetectionEvent().value(abstime=5, channel=3))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::append(")
    assert ".abstime = static_cast<" in code
    assert "DOWN" in code


def test_Append_param_requires_event_type():
    with pytest.raises(ValueError, match="event_type is required"):
        Append(Param("evt"))


def test_Append_param_accepts_bucket_event_type():
    node = Append(Param("h"), event_type=tcspc.HistogramEvent())
    params = node._parameters()
    assert len(params) == 1
    assert params[0][0].name == "h"
    assert params[0][1] == tcspc.HistogramEvent()._cpp_type_name()
    assert node._value_event_types() == (tcspc.HistogramEvent(),)
    assert "h" in node._param_encoders()


def test_Append_param_rejects_array_event_type():
    with pytest.raises(TypeError, match="not yet supported"):
        Append(Param("evt"), event_type=tcspc.DetectionPairEvent())


def test_Append_concrete_rejects_mismatched_event_type():
    with pytest.raises(ValueError, match="event_type does not match"):
        Append(
            tcspc.DetectionEvent().value(abstime=0, channel=0),
            event_type=tcspc.MarkerEvent(),
        )


def test_Append_param_codegen_references_params_struct():
    node = Append(Param("evt"), event_type=tcspc.DetectionEvent())
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert code.startswith("tcspc::append(")
    assert "params.z_evt" in code
    assert "DOWN" in code


def test_Prepend_compiles_and_fires_once_end_to_end():
    g = Graph()
    g.add_node(
        "prep",
        Prepend(tcspc.DetectionEvent().value(abstime=42, channel=1)),
    )
    g.add_node(
        "count",
        Count(tcspc.DetectionEvent(), AccessTag("c")),
        upstream="prep",
    )
    g.add_node(None, SinkAll(), upstream="count")
    ctx = ExecutionContext(CompiledGraph(g, (IntEvent,)))
    assert ctx.access(AccessTag("c")).count() == 0
    ctx.handle(10)
    assert ctx.access(AccessTag("c")).count() == 1
    ctx.handle(20)
    assert ctx.access(AccessTag("c")).count() == 1
    ctx.flush()


def test_Append_compiles_and_fires_at_flush_end_to_end():
    g = Graph()
    g.add_node(
        "app",
        Append(tcspc.DetectionEvent().value(abstime=42, channel=1)),
    )
    g.add_node(
        "count",
        Count(tcspc.DetectionEvent(), AccessTag("c")),
        upstream="app",
    )
    g.add_node(None, SinkAll(), upstream="count")
    ctx = ExecutionContext(CompiledGraph(g, ()))
    assert ctx.access(AccessTag("c")).count() == 0
    ctx.flush()
    assert ctx.access(AccessTag("c")).count() == 1
