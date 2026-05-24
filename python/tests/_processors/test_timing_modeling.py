# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._param import Param

TickEvent = _NamedEvent(_CppTypeName("int"))
StartEvent = _NamedEvent(_CppTypeName("long"))
StopEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_AddCountToPeriodicSequences_event_set():
    node = tcspc.AddCountToPeriodicSequences(10)
    (out,) = node._map_event_sets([(tcspc.PeriodicSequenceModelEvent(),)])
    assert out == (tcspc.RealLinearTimingEvent(),)


def test_AddCountToPeriodicSequences_codegen():
    code = tcspc.AddCountToPeriodicSequences(10)._cpp_expression(
        gencontext, DOWN
    )
    assert (
        "tcspc::add_count_to_periodic_sequences<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::count<std::size_t>" in code
    assert "DOWN" in code


def test_ConvertSequencesToStartStop_event_set():
    node = tcspc.ConvertSequencesToStartStop(
        TickEvent, StartEvent, StopEvent, 3
    )
    (out,) = node._map_event_sets([(TickEvent,)])
    cpp = [e._cpp_type_name() for e in out]
    assert TickEvent._cpp_type_name() not in cpp
    assert StartEvent._cpp_type_name() in cpp
    assert StopEvent._cpp_type_name() in cpp


def test_ConvertSequencesToStartStop_codegen():
    node = tcspc.ConvertSequencesToStartStop(
        TickEvent, StartEvent, StopEvent, 3
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::convert_sequences_to_start_stop<" in code
    assert "int" in code and "long" in code and "short" in code
    assert "tcspc::arg::count<std::size_t>" in code
    assert "DOWN" in code


def test_ExtrapolatePeriodicSequences_event_set():
    node = tcspc.ExtrapolatePeriodicSequences(2)
    (out,) = node._map_event_sets([(tcspc.PeriodicSequenceModelEvent(),)])
    assert out == (tcspc.RealOneShotTimingEvent(),)


def test_ExtrapolatePeriodicSequences_codegen():
    code = tcspc.ExtrapolatePeriodicSequences(2)._cpp_expression(
        gencontext, DOWN
    )
    assert (
        "tcspc::extrapolate_periodic_sequences<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::tick_index<std::size_t>" in code
    assert "DOWN" in code


def test_FitPeriodicSequences_event_set():
    node = tcspc.FitPeriodicSequences(TickEvent, 5, 1.0, 2.0, 0.1)
    (out,) = node._map_event_sets([(TickEvent,)])
    assert out == (tcspc.PeriodicSequenceModelEvent(),)


def test_FitPeriodicSequences_codegen():
    node = tcspc.FitPeriodicSequences(TickEvent, 5, 1.0, 2.0, 0.1)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::fit_periodic_sequences<\n                int," in code
    assert "tcspc::arg::length<std::size_t>" in code
    assert "tcspc::arg::min_interval<double>{1.0}" in code
    assert "tcspc::arg::max_interval<double>{2.0}" in code
    assert "tcspc::arg::max_mse<double>{0.1}" in code
    assert "DOWN" in code


def test_FitPeriodicSequences_params():
    node = tcspc.FitPeriodicSequences(
        TickEvent, Param("n"), Param("mn"), Param("mx"), Param("e")
    )
    assert len(node._parameters()) == 4


def test_RetimePeriodicSequences_event_set():
    node = tcspc.RetimePeriodicSequences(100)
    (out,) = node._map_event_sets([(tcspc.PeriodicSequenceModelEvent(),)])
    assert out == (tcspc.PeriodicSequenceModelEvent(),)


def test_RetimePeriodicSequences_rejects_other_events():
    node = tcspc.RetimePeriodicSequences(100)
    with pytest.raises(ValueError):
        node._map_event_sets(
            [
                (
                    tcspc.PeriodicSequenceModelEvent(),
                    _NamedEvent(_CppTypeName("int")),
                )
            ]
        )


def test_RetimePeriodicSequences_codegen():
    code = tcspc.RetimePeriodicSequences(100)._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::retime_periodic_sequences<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::max_time_shift<" in code
    assert "DOWN" in code
