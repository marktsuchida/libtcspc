# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import BucketEvent, _ByteEvent
from libtcspc._processors import UnbatchFromBytes

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_UnbatchFromBytes_accepts_bucket_of_bytes():
    node = UnbatchFromBytes(IntEvent)
    assert node._map_event_sets([(BucketEvent(_ByteEvent()),)]) == (
        (IntEvent,),
    )


def test_UnbatchFromBytes_rejects_non_byte_input():
    node = UnbatchFromBytes(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(IntEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_UnbatchFromBytes_codegen():
    node = UnbatchFromBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::unbatch_from_bytes<int>(" in code
    assert "DOWN" in code
