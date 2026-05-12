# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._bucket_sources import (
    NewDeleteBucketSource,
    RecyclingBucketSource,
)
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName, _size_type
from libtcspc._param import Param

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_NewDeleteBucketSource():
    bs = NewDeleteBucketSource(IntEvent)
    assert (
        bs._cpp_expression(gencontext)
        == "tcspc::new_delete_bucket_source<int>::create()"
    )


def test_RecyclingBucketSource_default():
    bs = RecyclingBucketSource(IntEvent)
    assert len(bs._parameters()) == 0
    assert "recycling_bucket_source<int, false, false>" in bs._cpp_expression(
        gencontext
    )


def test_RecyclingBucketSource_flags():
    assert "<int, true, false>" in RecyclingBucketSource(
        IntEvent, blocking=True
    )._cpp_expression(gencontext)
    assert "<int, false, true>" in RecyclingBucketSource(
        IntEvent, clear_recycled=True
    )._cpp_expression(gencontext)


def test_RecyclingBucketSource_max_bucket_count():
    bs = RecyclingBucketSource(IntEvent, max_bucket_count=42)
    assert len(bs._parameters()) == 0
    assert "42uLL" in bs._cpp_expression(gencontext)


def test_RecyclingBucketSource_max_bucket_count_param():
    bs = RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc"))
    assert len(bs._parameters()) == 1
    assert bs._parameters()[0] == (Param("mbc"), _size_type)
    assert "params.mbc" in bs._cpp_expression(gencontext)


def test_RecyclingBucketSource_max_bucket_count_negative_is_error():
    with pytest.raises(ValueError):
        RecyclingBucketSource(IntEvent, max_bucket_count=-1)

    with pytest.raises(ValueError):
        RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc", -1))
