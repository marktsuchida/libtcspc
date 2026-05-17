# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName
from libtcspc._processors import SinkAll

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
