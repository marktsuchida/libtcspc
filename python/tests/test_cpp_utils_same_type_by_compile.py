# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._cpp_utils import _CppTypeName, _same_type_by_compile


def test_string_equal():
    assert _same_type_by_compile(_CppTypeName("int"), _CppTypeName("int"))
    assert _same_type_by_compile(
        _CppTypeName("std::vector<int>"), _CppTypeName("std::vector<int>")
    )


def test_not_string_equal():
    assert _same_type_by_compile(
        _CppTypeName("unsigned"), _CppTypeName("std::make_unsigned_t<int>")
    )


def test_not_equal():
    assert not _same_type_by_compile(_CppTypeName("int"), _CppTypeName("long"))
    assert not _same_type_by_compile(
        _CppTypeName("std::vector<int>"), _CppTypeName("std::vector<long>")
    )
