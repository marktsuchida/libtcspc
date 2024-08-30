# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
import typing
from collections.abc import Iterable
from textwrap import dedent

import cppyy

cppyy.include("type_traits")

CppTypeName = typing.NewType("CppTypeName", str)

_cpp_name_counter = itertools.count()


@functools.cache
def _is_same_type_impl(t0: CppTypeName, t1: CppTypeName) -> bool:
    result_name = f"is_same_type_impl_{next(_cpp_name_counter)}"
    cppyy.cppdef(
        dedent(f"""\
            namespace tcspc::py::cpp_utils {{
                constexpr bool {result_name} = std::is_same_v<{t0}, {t1}>;
            }}""")
    )
    return getattr(cppyy.gbl.tcspc.py.cpp_utils, result_name)


def is_same_type(t0: CppTypeName, t1: CppTypeName) -> bool:
    if t0 == t1:
        return True
    # Always use ascending lexicographical order to minimize duplicate checks.
    if t0 > t1:
        t0, t1 = t1, t0
    return _is_same_type_impl(t0, t1)


def contains_type(s: Iterable[CppTypeName], t: CppTypeName) -> bool:
    return any(is_same_type(t, t1) for t1 in s)


def quote_string(s: str) -> str:
    return '"{}"'.format(
        s.replace("\\", "\\\\")
        .replace("'", "\\'")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\t", "\\t")
    )
