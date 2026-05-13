# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._cpp_utils import _identifier_from_string


def test__identifier_from_string():
    assert _identifier_from_string("abc") == "z_abc"
    assert _identifier_from_string("0bc") == "z_0bc"
    assert _identifier_from_string("a0c") == "z_a0c"
    assert _identifier_from_string("01c") == "z_01c"
    assert _identifier_from_string("_bc") == "z_Q5fbc"
    assert _identifier_from_string("_b_") == "z_Q5fbQ5f"
    assert _identifier_from_string("a-b") == "z_aQ2db"
    assert _identifier_from_string("Q") == "z_Q51"
    assert _identifier_from_string("") == "z_"
