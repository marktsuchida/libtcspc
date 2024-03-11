# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from __future__ import annotations

import functools
import itertools

import cppyy

cppyy.include("type_traits")

_cppyy_check_counter = itertools.count()


@functools.cache
def _is_same_cpp_type_impl(t0: str, t1: str) -> bool:
    result_name = f"is_same_cpp_type_impl_{next(_cppyy_check_counter)}"
    cppyy.cppdef(f"""namespace tcspc::cppyy_events {{
constexpr bool {result_name} = std::is_same_v<{t0}, {t1}>;
}}""")
    return getattr(cppyy.gbl.tcspc.cppyy_events, result_name)


def _is_same_cpp_type(t0: str, t1: str) -> bool:
    if t0 == t1:
        return True
    # Always use ascending lexicographical order to minimize duplicate checks.
    if t0 > t1:
        t0, t1 = t1, t0
    return _is_same_cpp_type_impl(t0, t1)


class EventType:
    def __init__(self, cpp_type: str) -> None:
        self.cpp_type = cpp_type

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}({self.cpp_type})>"

    def __eq__(self, other) -> bool:
        return isinstance(other, EventType) and _is_same_cpp_type(
            self.cpp_type, other.cpp_type
        )
