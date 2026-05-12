# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName
from libtcspc._processors import NullSink

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_NullSink_has_no_output_ports():
    node = NullSink()
    assert node.outputs() == ()


def test_NullSink_accepts_any_input_event_set():
    node = NullSink()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == ()
    assert node._map_event_sets([()]) == ()


def test_NullSink_codegen_is_tcspc_null_sink():
    node = NullSink()
    assert node._cpp_expression(gencontext, []) == "tcspc::null_sink()"
