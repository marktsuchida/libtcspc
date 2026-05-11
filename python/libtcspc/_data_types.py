# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import Any, TypedDict

import numpy as np
from typing_extensions import Unpack

from ._cpp_utils import CppTypeName, cpp_type_from_dtype


class _DataTypes(TypedDict, total=False):
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


class DataTypes:
    def __init__(self, **kwargs: Unpack[_DataTypes]) -> None:
        def typ(category: str) -> CppTypeName:
            key = f"{category}_type"
            if key not in kwargs:
                return CppTypeName(
                    f"tcspc::default_data_types::{category}_type"
                )
            value = kwargs[key]  # type: ignore[literal-required]
            cpp_type = cpp_type_from_dtype(value)
            allowed = _SLOT_RULES[category]
            kind = np.dtype(value).kind
            if kind not in allowed:
                raise TypeError(
                    f"DataTypes.{category}_type must be "
                    f"{_SLOT_DESCRIPTION[allowed]} dtype, got "
                    f"{np.dtype(value)!s}"
                )
            return cpp_type

        tparams = ", ".join(
            (
                typ("abstime"),
                typ("channel"),
                typ("difftime"),
                typ("count"),
                typ("datapoint"),
                typ("bin_index"),
                typ("bin"),
            )
        )
        self._type_set_class = CppTypeName(
            f"tcspc::parameterized_data_types<{tparams}>"
        )

    def cpp_type_name(self) -> CppTypeName:
        return self._type_set_class
