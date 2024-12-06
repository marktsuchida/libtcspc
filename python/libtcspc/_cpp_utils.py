# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "CppExpression",
    "CppFunctionScopeDefs",
    "CppIdentifier",
    "CppNamespaceScopeDefs",
    "CppTypeName",
    "ModuleCodeFragment",
    "contains_type",
    "is_same_type",
    "quote_string",
    "run_cpp_prog",
]

import functools
import subprocess
import typing
from collections.abc import Iterable
from dataclasses import dataclass

from . import _include, _odext

_builder = _odext.Builder(
    binary_type="executable",
    cpp_std="c++17",
    include_dirs=(_include.libtcspc_include_dir(),),
    pch_includes=("libtcspc/tcspc.hpp",),
)


def run_cpp_prog(code: str) -> int:
    _builder.set_code(code)
    exe_path = _builder.build()
    result = subprocess.run(exe_path)
    return result.returncode


CppTypeName = typing.NewType("CppTypeName", str)
CppIdentifier = typing.NewType("CppIdentifier", str)
CppExpression = typing.NewType("CppExpression", str)
CppFunctionScopeDefs = typing.NewType("CppFunctionScopeDefs", str)
CppNamespaceScopeDefs = typing.NewType("CppNamespaceScopeDefs", str)


@dataclass
class ModuleCodeFragment:
    includes: tuple[str, ...]
    sys_includes: tuple[str, ...]
    namespace_scope_defs: CppNamespaceScopeDefs
    nanobind_defs: CppFunctionScopeDefs


@functools.cache
def _is_same_type_impl(t0: CppTypeName, t1: CppTypeName) -> bool:
    return (
        run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>

            int main() {{
                constexpr bool result = std::is_same_v<{t0}, {t1}>;
                return result ? 1 : 0;
            }}
            """)
        != 0
    )


def is_same_type(t0: CppTypeName, t1: CppTypeName) -> bool:
    if t0 == t1:
        return True
    # Always use ascending lexicographical order to minimize duplicate checks.
    if t0 > t1:
        t0, t1 = t1, t0
    return _is_same_type_impl(t0, t1)


def contains_type(s: Iterable[CppTypeName], t: CppTypeName) -> bool:
    return any(is_same_type(t, t1) for t1 in s)


def quote_string(s: str) -> CppExpression:
    return CppExpression(
        '"{}"'.format(
            s.replace("\\", "\\\\")
            .replace("'", "\\'")
            .replace('"', '\\"')
            .replace("\n", "\\n")
            .replace("\t", "\\t")
        )
    )
