# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import BucketEvent, _ByteEvent
from libtcspc._processors import ViewAsBytes

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_ViewAsBytes_accepts_bucket_of_event_type():
    node = ViewAsBytes(IntEvent)
    assert node._map_event_sets([(BucketEvent(IntEvent),)]) == (
        (BucketEvent(_ByteEvent()),),
    )


def test_ViewAsBytes_rejects_mismatched_bucket():
    node = ViewAsBytes(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(OtherEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_ViewAsBytes_codegen():
    node = ViewAsBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::view_as_bytes(" in code
    assert "DOWN" in code
