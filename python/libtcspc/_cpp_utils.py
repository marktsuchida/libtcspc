# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import subprocess
import typing
from collections.abc import Iterable
from dataclasses import dataclass

import numpy as np
from typing_extensions import Self

from . import _include, _odext

_builder = _odext.Builder(
    binary_type="executable",
    cpp_std="c++20",
    include_dirs=(_include._libtcspc_include_dir(),),
    pch_includes=("libtcspc/tcspc.hpp",),
)


def _run_cpp_prog(code: str) -> int:
    _builder.set_code(code)
    exe_path = _builder.build()
    result = subprocess.run(exe_path)
    return result.returncode


_CppTypeName = typing.NewType("_CppTypeName", str)
_CppIdentifier = typing.NewType("_CppIdentifier", str)
_CppExpression = typing.NewType("_CppExpression", str)
_CppFunctionScopeDefs = typing.NewType("_CppFunctionScopeDefs", str)
_CppClassScopeDefs = typing.NewType("_CppClassScopeDefs", str)
_CppNamespaceScopeDefs = typing.NewType("_CppNamespaceScopeDefs", str)


_byte_type = _CppTypeName("std::byte")
_size_type = _CppTypeName("std::size_t")
_uint8_type = _CppTypeName("std::uint8_t")
_int8_type = _CppTypeName("std::int8_t")
_uint16_type = _CppTypeName("std::uint16_t")
_int16_type = _CppTypeName("std::int16_t")
_uint32_type = _CppTypeName("std::uint32_t")
_int32_type = _CppTypeName("std::int32_t")
_uint64_type = _CppTypeName("std::uint64_t")
_int64_type = _CppTypeName("std::int64_t")
_float32_type = _CppTypeName("float")
_float64_type = _CppTypeName("double")
_string_type = _CppTypeName("std::string")
_bool_type = _CppTypeName("bool")


_INT_BY_SIZE = {
    1: _int8_type,
    2: _int16_type,
    4: _int32_type,
    8: _int64_type,
}
_UINT_BY_SIZE = {
    1: _uint8_type,
    2: _uint16_type,
    4: _uint32_type,
    8: _uint64_type,
}
_FLOAT_BY_SIZE = {
    4: _float32_type,
    8: _float64_type,
}


def _cpp_type_from_dtype(dtype_like: object) -> _CppTypeName:
    """Convert a NumPy dtype-like value to a _CppTypeName.

    Accepts anything ``np.dtype()`` accepts (scalar types like ``np.uint16``,
    dtype objects, or strings like ``"uint16"``). Returns the matching
    ``std::<int|uint><N>_t`` / ``float`` / ``double`` ``_CppTypeName``.
    Rejects non-native byte order, bool, complex, datetime, object,
    structured types, and unsupported widths with ``TypeError``.
    """
    try:
        dt = np.dtype(typing.cast(typing.Any, dtype_like))
    except TypeError as e:
        raise TypeError(f"not a NumPy dtype: {dtype_like!r}") from e
    if dt.byteorder not in ("=", "|"):
        raise TypeError(f"non-native byte order not supported: {dtype_like!r}")
    table = {
        "i": _INT_BY_SIZE,
        "u": _UINT_BY_SIZE,
        "f": _FLOAT_BY_SIZE,
    }.get(dt.kind)
    if table is None or dt.itemsize not in table:
        raise TypeError(
            f"unsupported dtype {dt!s} (must be integer or float of "
            f"supported width)"
        )
    return table[dt.itemsize]


@dataclass
class _ModuleCodeFragment:
    includes: tuple[str, ...]
    sys_includes: tuple[str, ...]
    namespace_scope_defs: tuple[_CppNamespaceScopeDefs, ...]
    nanobind_defs: tuple[_CppFunctionScopeDefs, ...]

    def __add__(self, rhs: Self) -> Self:
        return type(self)(
            self.includes + rhs.includes,
            self.sys_includes + rhs.sys_includes,
            self.namespace_scope_defs + rhs.namespace_scope_defs,
            self.nanobind_defs + rhs.nanobind_defs,
        )


# Caching here is sound because any `nt_<hash>` name referenced through
# `_struct_definitions_referenced_by` and any `<name>_<hash>` custom event name
# referenced through `_event_definitions_referenced_by` are content-addressed:
# the same name always implies the same struct body within a process.
@functools.cache
def _is_same_type_impl(t0: _CppTypeName, t1: _CppTypeName) -> bool:
    from ._events import _event_definitions_referenced_by
    from ._numeric_traits import _struct_definitions_referenced_by

    # An abstime custom event's definition references an `nt_<hash>` trait
    # struct, so resolve traits from the event definitions too, and emit traits
    # before events.
    event_defs = _event_definitions_referenced_by((t0, t1))
    trait_defs = _struct_definitions_referenced_by((t0, t1, *event_defs))
    defs = trait_defs + event_defs
    prelude = (
        "namespace {\n" + "\n".join(defs) + "\n} // namespace\n"
        if defs
        else ""
    )
    return (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>

            {prelude}
            int main() {{
                constexpr bool result = std::is_same_v<{t0}, {t1}>;
                return result ? 1 : 0;
            }}
            """)
        != 0
    )


def _is_same_type(t0: _CppTypeName, t1: _CppTypeName) -> bool:
    if t0 == t1:
        return True
    # Always use ascending lexicographical order to minimize duplicate checks.
    if t0 > t1:
        t0, t1 = t1, t0
    return _is_same_type_impl(t0, t1)


def _contains_type(s: Iterable[_CppTypeName], t: _CppTypeName) -> bool:
    return any(_is_same_type(t, t1) for t1 in s)


def _quote_string(s: str) -> _CppExpression:
    return _CppExpression(
        '"{}"'.format(
            s.replace("\\", "\\\\")
            .replace("'", "\\'")
            .replace('"', '\\"')
            .replace("\n", "\\n")
            .replace("\t", "\\t")
        )
    )


def _identifier_from_string(s: str) -> _CppIdentifier:
    # Map strings to valid and safe C++ identifiers by escaping with 'Q'.
    # Use a fixed prefix (guarantees letter start). Importantly, C++ has
    # no keyword beginning with 'z_'.
    out = ["z_"]
    for b in s.encode():
        c = chr(b)
        safe = (
            ("a" <= c <= "z")
            or ("A" <= c <= "Z" and c != "Q")
            or ("0" <= c <= "9")
        )
        if safe:
            out.append(c)
        else:
            out.append(f"Q{b:02x}")
    return _CppIdentifier("".join(out))
