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
    "byte_type",
    "contains_type",
    "float32_type",
    "float64_type",
    "identifier_from_string",
    "int16_type",
    "int32_type",
    "int64_type",
    "int8_type",
    "is_same_type",
    "quote_string",
    "run_cpp_prog",
    "size_type",
    "string_type",
    "uint16_type",
    "uint32_type",
    "uint64_type",
    "uint8_type",
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


byte_type = CppTypeName("std::byte")
size_type = CppTypeName("std::size_t")
uint8_type = CppTypeName("std::uint8_t")
int8_type = CppTypeName("std::int8_t")
uint16_type = CppTypeName("std::uint16_t")
int16_type = CppTypeName("std::int16_t")
uint32_type = CppTypeName("std::uint32_t")
int32_type = CppTypeName("std::int32_t")
uint64_type = CppTypeName("std::uint64_t")
int64_type = CppTypeName("std::int64_t")
float32_type = CppTypeName("float")
float64_type = CppTypeName("double")
string_type = CppTypeName("std::string")


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


def identifier_from_string(s: str) -> CppIdentifier:
    # Convert special chars to underscores, but record their character codes
    # and append as hex at the end. All leading digits are treated as special
    # chars. For now, treat all non-ASCII characters as "special" and encode.
    specials: list[int] = []
    ret_chars: list[str] = []
    encoded = s.encode()
    digits = b"0123456789"
    allowed = b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvxxyz"
    while len(encoded) > 0 and encoded[0:1] in digits:
        specials.append(encoded[0])
        ret_chars.append("_")
        encoded = encoded[1:]
    for b in encoded:
        if chr(b).encode() in allowed:
            ret_chars.append(chr(b))
        else:
            specials.append(b)
            ret_chars.append("_")
    return CppIdentifier(
        "".join(ret_chars) + "_" + "".join(format(c, "02X") for c in specials)
    )
