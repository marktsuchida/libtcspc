# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName
from libtcspc._processors import SinkEvents

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))
ThirdEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_SinkEvents_has_no_output_ports():
    node = SinkEvents(IntEvent, OtherEvent)
    assert node.outputs() == ()


def test_SinkEvents_accepts_configured_events():
    node = SinkEvents(IntEvent, OtherEvent)
    assert node._map_event_sets([(IntEvent,)]) == ()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == ()


def test_SinkEvents_rejects_unconfigured_event():
    node = SinkEvents(IntEvent, OtherEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, ThirdEvent)])


def test_SinkEvents_rejects_wrong_number_of_inputs():
    node = SinkEvents(IntEvent, OtherEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([])
    with pytest.raises(ValueError):
        node._map_event_sets([(), ()])


def test_SinkEvents_codegen_lists_all_event_types():
    node = SinkEvents(IntEvent, OtherEvent)
    code = node._cpp_expression(gencontext, [])
    assert "tcspc::sink_events<int, long>()" in code
