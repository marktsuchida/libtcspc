# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools

import cppyy

cppyy.include("type_traits")

_cppyy_check_counter = itertools.count()


@functools.cache
def _is_same_type_impl(t0: str, t1: str) -> bool:
    result_name = f"is_same_type_impl_{next(_cppyy_check_counter)}"
    cppyy.cppdef(f"""namespace tcspc::cppyy_cpp_utils {{
constexpr bool {result_name} = std::is_same_v<{t0}, {t1}>;
}}""")
    return getattr(cppyy.gbl.tcspc.cppyy_cpp_utils, result_name)


def is_same_type(t0: str, t1: str) -> bool:
    if t0 == t1:
        return True
    # Always use ascending lexicographical order to minimize duplicate checks.
    if t0 > t1:
        t0, t1 = t1, t0
    return _is_same_type_impl(t0, t1)
