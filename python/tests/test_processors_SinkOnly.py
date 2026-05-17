# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName
from libtcspc._processors import SinkOnly

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))
ThirdEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_SinkOnly_has_no_output_ports():
    node = SinkOnly(IntEvent, OtherEvent)
    assert node.outputs() == ()


def test_SinkOnly_accepts_configured_events():
    node = SinkOnly(IntEvent, OtherEvent)
    assert node._map_event_sets([(IntEvent,)]) == ()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == ()


def test_SinkOnly_rejects_unconfigured_event():
    node = SinkOnly(IntEvent, OtherEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, ThirdEvent)])


def test_SinkOnly_rejects_wrong_number_of_inputs():
    node = SinkOnly(IntEvent, OtherEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([])
    with pytest.raises(ValueError):
        node._map_event_sets([(), ()])


def test_SinkOnly_codegen_lists_all_event_types():
    node = SinkOnly(IntEvent, OtherEvent)
    code = node._cpp_expression(gencontext, [])
    assert "tcspc::sink_only<int, long>()" in code
