# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import contextlib
import contextvars
import hashlib
import re
from collections.abc import Iterable, Iterator
from typing import Any, TypedDict

import numpy as np
from typing_extensions import Unpack

from ._cpp_utils import _cpp_type_from_dtype, _CppTypeName


class _NumericTraits(TypedDict, total=False):
    abstime_type: Any
    channel_type: Any
    difftime_type: Any
    count_type: Any
    datapoint_type: Any
    bin_index_type: Any
    bin_type: Any


_SLOT_RULES: dict[str, frozenset[str]] = {
    "abstime": frozenset({"i", "u"}),
    "channel": frozenset({"i", "u"}),
    "difftime": frozenset({"i"}),
    "count": frozenset({"i", "u"}),
    "datapoint": frozenset({"i", "u"}),
    "bin_index": frozenset({"u"}),
    "bin": frozenset({"i", "u"}),
}

_SLOT_DESCRIPTION: dict[frozenset[str], str] = {
    frozenset({"i", "u"}): "an integer",
    frozenset({"i"}): "a signed integer",
    frozenset({"u"}): "an unsigned integer",
}


_referenced_traits: contextvars.ContextVar[dict[str, str] | None] = (
    contextvars.ContextVar("_referenced_traits", default=None)
)


@contextlib.contextmanager
def _collecting_referenced_traits() -> Iterator[dict[str, str]]:
    registry: dict[str, str] = {}
    token = _referenced_traits.set(registry)
    try:
        yield registry
    finally:
        _referenced_traits.reset(token)


# Process-wide registry of all struct definitions ever produced. Keyed by the
# content-addressed struct name, so duplicates collapse harmlessly. Used to
# resolve `nt_<hash>` references in `_is_same_type` test compiles.
_known_traits_definitions: dict[str, str] = {}

_NT_NAME_PATTERN = re.compile(r"\bnt_[0-9a-f]{16}\b")


def _struct_definitions_referenced_by(typenames: Iterable[str]) -> list[str]:
    seen: dict[str, str] = {}
    for typename in typenames:
        for name in _NT_NAME_PATTERN.findall(typename):
            if name in _known_traits_definitions and name not in seen:
                seen[name] = _known_traits_definitions[name]
    return list(seen.values())


class NumericTraits:
    """Set of integer types used by events and processors that are parameterised by numeric traits.

    This is the Python-side counterpart to ``tcspc::default_numeric_traits``
    on the C++ side. Constructing `NumericTraits` with no arguments yields
    the default set; supply one or more keyword arguments to override
    individual slots. Each override must be a NumPy dtype object, dtype
    instance, or dtype string, and must satisfy the kind constraint
    documented below.

    Parameters
    ----------
    abstime_type : dtype-like, optional
        Type used for absolute time (macrotime). Must be a signed or
        unsigned integer dtype. Default ``numpy.int64``.
    channel_type : dtype-like, optional
        Type used for channel numbers. Must be a signed or unsigned
        integer dtype. Default ``numpy.int32``.
    difftime_type : dtype-like, optional
        Type used for difference times (microtime). Must be a **signed**
        integer dtype. Default ``numpy.int32``.
    count_type : dtype-like, optional
        Type used for counts of detections. Must be a signed or
        unsigned integer dtype. Default ``numpy.uint32``.
    datapoint_type : dtype-like, optional
        Type used for histogram datapoint values. Must be a signed or
        unsigned integer dtype. Default ``numpy.int32``.
    bin_index_type : dtype-like, optional
        Type used for histogram bin indices. Must be an **unsigned**
        integer dtype. Default ``numpy.uint16``.
    bin_type : dtype-like, optional
        Type used for histogram bin values (counts). Must be a signed
        or unsigned integer dtype. Default ``numpy.uint16``.

    Notes
    -----
    Unknown keyword arguments raise ``TypeError`` from the underlying
    ``TypedDict``; values whose dtype kind violates the constraint
    above raise ``TypeError`` from the constructor.

    See Also
    --------
    :cpp:`tcspc::default_numeric_traits`
        The C++ default numeric traits.
    """

    def __init__(self, **kwargs: Unpack[_NumericTraits]) -> None:
        overrides: dict[str, _CppTypeName] = {}
        for category, allowed in _SLOT_RULES.items():
            key = f"{category}_type"
            if key not in kwargs:
                continue
            value = kwargs[key]  # type: ignore[literal-required]
            cpp_type = _cpp_type_from_dtype(value)
            kind = np.dtype(value).kind
            if kind not in allowed:
                raise TypeError(
                    f"NumericTraits.{category}_type must be "
                    f"{_SLOT_DESCRIPTION[allowed]} dtype, got "
                    f"{np.dtype(value)!s}"
                )
            overrides[key] = cpp_type

        if not overrides:
            self._struct_name = _CppTypeName("tcspc::default_numeric_traits")
            self._struct_definition: str | None = None
        else:
            canonical = ";".join(
                f"{k}={v}" for k, v in sorted(overrides.items())
            )
            digest = hashlib.sha256(canonical.encode()).hexdigest()[:16]
            self._struct_name = _CppTypeName(f"nt_{digest}")
            self._struct_definition = (
                f"struct {self._struct_name} : tcspc::default_numeric_traits {{\n"
                + "".join(
                    f"    using {slot} = {typ};\n"
                    for slot, typ in overrides.items()
                )
                + "};"
            )
            _known_traits_definitions.setdefault(
                str(self._struct_name), self._struct_definition
            )

    def _cpp_type_name(self) -> _CppTypeName:
        if self._struct_definition is not None:
            registry = _referenced_traits.get()
            if registry is not None:
                registry[str(self._struct_name)] = self._struct_definition
        return self._struct_name
