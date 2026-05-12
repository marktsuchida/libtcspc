# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._cpp_utils import _identifier_from_string


def test__identifier_from_string():
    assert _identifier_from_string("abc") == "abc_"
    assert _identifier_from_string("0bc") == "_bc_30"
    assert _identifier_from_string("a0c") == "a0c_"
    assert _identifier_from_string("01c") == "__c_3031"
    assert _identifier_from_string("_bc") == "_bc_5F"
    assert _identifier_from_string("_b_") == "_b__5F5F"
    assert _identifier_from_string("a-b") == "a_b_2D"
