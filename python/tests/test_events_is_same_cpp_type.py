# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import cppyy
from libtcspc._events import _is_same_cpp_type

cppyy.include("deque")
cppyy.include("type_traits")


def test_string_equal():
    assert _is_same_cpp_type("int", "int")
    assert _is_same_cpp_type("std::deque<int>", "std::deque<int>")


def test_not_string_equal():
    assert _is_same_cpp_type("unsigned", "std::make_unsigned_t<int>")


def test_not_equal():
    assert not _is_same_cpp_type("int", "long")
    assert not _is_same_cpp_type("std::deque<int>", "std::deque<long>")


def test_result_cached(mocker):
    assert _is_same_cpp_type("unsigned", "std::make_unsigned_t<int>")
    assert not _is_same_cpp_type("long", "std::make_signed_t<unsigned>")
    mocker.patch(
        "cppyy.cppdef",
        side_effect=AssertionError(
            "cppdef() called where cached result should be used"
        ),
    )
    assert _is_same_cpp_type("unsigned", "std::make_unsigned_t<int>")
    assert _is_same_cpp_type("std::make_unsigned_t<int>", "unsigned")
    assert not _is_same_cpp_type("long", "std::make_signed_t<unsigned>")
    assert not _is_same_cpp_type("std::make_signed_t<unsigned>", "long")
