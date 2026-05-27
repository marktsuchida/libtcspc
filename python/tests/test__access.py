# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import dataclasses

import pytest
from libtcspc._access import AccessTag, _UniqueBinMapperAccessorSpec


def test_AccessTag_equality_and_hash():
    assert AccessTag("a") == AccessTag("a")
    assert AccessTag("a") != AccessTag("b")
    assert hash(AccessTag("a")) == hash(AccessTag("a"))
    d = {AccessTag("a"): 1, AccessTag("b"): 2}
    assert d[AccessTag("a")] == 1
    s = {AccessTag("a"), AccessTag("a"), AccessTag("b")}
    assert s == {AccessTag("a"), AccessTag("b")}


def test_AccessTag_is_frozen():
    t = AccessTag("a")
    with pytest.raises((dataclasses.FrozenInstanceError, AttributeError)):
        t.tag = "b"  # type: ignore[misc]


def test_AccessTag_rejects_empty_string():
    with pytest.raises(ValueError, match="empty"):
        AccessTag("")


def test_AccessTag_accepts_non_identifier_characters():
    AccessTag("a-b.c/d")
    AccessTag("日本語")
    AccessTag(" ")


def test_AccessTag_context_method_name_prefix():
    assert AccessTag("hello")._context_method_name() == "access__z_hello"


def test_AccessTag_context_method_name_sanitizes_special_chars():
    assert AccessTag("a-b")._context_method_name() == "access__z_aQ2db"


def test_AccessTag_context_method_name_distinct_for_distinct_tags():
    assert (
        AccessTag("foo")._context_method_name()
        != AccessTag("foo-bar")._context_method_name()
    )


def test_UniqueBinMapperAccessorSpec():
    spec = _UniqueBinMapperAccessorSpec()
    assert spec.py_class_name() == "UniqueBinMapperAccessor"
    assert "values" in spec.cpp_methods()
    assert "unique_bin_mapper_accessor" in (spec._cpp_type_name())
