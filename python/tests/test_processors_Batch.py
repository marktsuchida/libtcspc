# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import BucketEvent
from libtcspc._param import Param
from libtcspc._processors import Batch

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_Batch_accepts_only_matching_event():
    node = Batch(IntEvent)
    assert node._map_event_sets([(IntEvent,)]) == ((BucketEvent(IntEvent),),)
    with pytest.raises(ValueError):
        node._map_event_sets([(OtherEvent,)])


def test_Batch_default_batch_size_is_65536():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::size_t{65536uLL}" in code


def test_Batch_batch_size_int():
    node = Batch(IntEvent, batch_size=42)
    assert len(node._parameters()) == 0
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::size_t{42uLL}" in code


def test_Batch_batch_size_param():
    node = Batch(IntEvent, batch_size=Param("bs"))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "params.bs" in code
    # FIXME: Batch does not currently override _parameters(), so a
    # Param-valued batch_size is referenced in codegen but not declared.
    assert len(node._parameters()) == 0


def test_Batch_default_bucket_source_is_recycling():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "recycling_bucket_source<int," in code


def test_Batch_codegen_calls_tcspc_batch():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::batch<int>(" in code
    assert "DOWN" in code
