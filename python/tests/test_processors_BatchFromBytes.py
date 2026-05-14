# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._bucket_sources import RecyclingBucketSource
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _size_type,
)
from libtcspc._events import BucketEvent, _ByteEvent
from libtcspc._param import Param
from libtcspc._processors import BatchFromBytes

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_BatchFromBytes_accepts_bucket_of_bytes():
    node = BatchFromBytes(IntEvent)
    assert node._map_event_sets([(BucketEvent(_ByteEvent()),)]) == (
        (BucketEvent(IntEvent),),
    )


def test_BatchFromBytes_rejects_non_byte_input():
    node = BatchFromBytes(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(IntEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_BatchFromBytes_codegen_calls_tcspc_batch_from_bytes():
    node = BatchFromBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::batch_from_bytes<int>(" in code
    assert "DOWN" in code


def test_BatchFromBytes_default_bucket_source_is_recycling():
    node = BatchFromBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "recycling_bucket_source<int," in code


def test_BatchFromBytes_buffer_provider_params_propagate():
    bp = RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc"))
    node = BatchFromBytes(IntEvent, buffer_provider=bp)
    params = node._parameters()
    assert (Param("mbc"), _size_type) in params
