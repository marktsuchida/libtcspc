# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._bucket_sources import (
    NewDeleteBucketSource,
    RecyclingBucketSource,
)
from libtcspc._codegen import CodeGenerationContext
from libtcspc._cpp_utils import CppIdentifier, CppTypeName, size_type
from libtcspc._events import EventType
from libtcspc._param import Param

IntEvent = EventType(CppTypeName("int"))

gencontext = CodeGenerationContext(
    CppIdentifier("ctx"), CppIdentifier("params")
)


def test_NewDeleteBucketSource():
    bs = NewDeleteBucketSource(IntEvent)
    assert (
        bs.cpp_expression(gencontext)
        == "tcspc::new_delete_bucket_source<int>::create()"
    )


def test_RecyclingBucketSource_default():
    bs = RecyclingBucketSource(IntEvent)
    assert len(bs.parameters()) == 0
    assert "recycling_bucket_source<int, false, false>" in bs.cpp_expression(
        gencontext
    )


def test_RecyclingBucketSource_flags():
    assert "<int, true, false>" in RecyclingBucketSource(
        IntEvent, blocking=True
    ).cpp_expression(gencontext)
    assert "<int, false, true>" in RecyclingBucketSource(
        IntEvent, clear_recycled=True
    ).cpp_expression(gencontext)


def test_RecyclingBucketSource_max_bucket_count():
    bs = RecyclingBucketSource(IntEvent, max_bucket_count=42)
    assert len(bs.parameters()) == 0
    assert "42uLL" in bs.cpp_expression(gencontext)


def test_RecyclingBucketSource_max_bucket_count_param():
    bs = RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc"))
    assert len(bs.parameters()) == 1
    assert bs.parameters()[0] == (Param("mbc"), size_type)
    assert "params.mbc" in bs.cpp_expression(gencontext)


def test_RecyclingBucketSource_max_bucket_count_negative_is_error():
    with pytest.raises(ValueError):
        RecyclingBucketSource(IntEvent, max_bucket_count=-1)

    with pytest.raises(ValueError):
        RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc", -1))
