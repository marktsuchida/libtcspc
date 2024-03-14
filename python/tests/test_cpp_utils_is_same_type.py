# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import cppyy
from libtcspc._cpp_utils import is_same_type

cppyy.include("deque")
cppyy.include("type_traits")


def test_string_equal():
    assert is_same_type("int", "int")
    assert is_same_type("std::deque<int>", "std::deque<int>")


def test_not_string_equal():
    assert is_same_type("unsigned", "std::make_unsigned_t<int>")


def test_not_equal():
    assert not is_same_type("int", "long")
    assert not is_same_type("std::deque<int>", "std::deque<long>")


def test_result_cached(mocker):
    assert is_same_type("unsigned", "std::make_unsigned_t<int>")
    assert not is_same_type("long", "std::make_signed_t<unsigned>")
    mocker.patch(
        "cppyy.cppdef",
        side_effect=AssertionError(
            "cppdef() called where cached result should be used"
        ),
    )
    assert is_same_type("unsigned", "std::make_unsigned_t<int>")
    assert is_same_type("std::make_unsigned_t<int>", "unsigned")
    assert not is_same_type("long", "std::make_signed_t<unsigned>")
    assert not is_same_type("std::make_signed_t<unsigned>", "long")
