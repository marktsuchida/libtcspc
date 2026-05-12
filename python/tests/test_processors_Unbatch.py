# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import BucketEvent
from libtcspc._processors import Unbatch

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Unbatch_accepts_only_matching_bucket_event():
    node = Unbatch(BucketEvent(IntEvent))
    assert node._map_event_sets([(BucketEvent(IntEvent),)]) == ((IntEvent,),)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(OtherEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_Unbatch_codegen_calls_tcspc_unbatch():
    node = Unbatch(BucketEvent(IntEvent))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::unbatch<tcspc::bucket<int>>(" in code
    assert "DOWN" in code
