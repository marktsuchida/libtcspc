# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._cpp_utils import CppTypeName, is_same_type


def test_string_equal():
    assert is_same_type(CppTypeName("int"), CppTypeName("int"))
    assert is_same_type(
        CppTypeName("std::vector<int>"), CppTypeName("std::vector<int>")
    )


def test_not_string_equal():
    assert is_same_type(
        CppTypeName("unsigned"), CppTypeName("std::make_unsigned_t<int>")
    )


def test_not_equal():
    assert not is_same_type(CppTypeName("int"), CppTypeName("long"))
    assert not is_same_type(
        CppTypeName("std::vector<int>"), CppTypeName("std::vector<long>")
    )
