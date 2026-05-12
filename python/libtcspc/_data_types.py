# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import Any, TypedDict

import numpy as np
from typing_extensions import Unpack

from ._cpp_utils import _cpp_type_from_dtype, _CppTypeName


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
    """Set of integer types used by events and processors that are parameterised by data types.

    This is the Python-side counterpart to ``tcspc::default_data_types``
    on the C++ side. Constructing `DataTypes` with no arguments yields
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
    :cpp:`tcspc::default_data_types`
        The C++ default data type set.
    :cpp:`tcspc::parameterized_data_types`
        The C++ template used to assemble a custom set.
    """

    def __init__(self, **kwargs: Unpack[_DataTypes]) -> None:
        def typ(category: str) -> _CppTypeName:
            key = f"{category}_type"
            if key not in kwargs:
                return _CppTypeName(
                    f"tcspc::default_data_types::{category}_type"
                )
            value = kwargs[key]  # type: ignore[literal-required]
            cpp_type = _cpp_type_from_dtype(value)
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
        self._type_set_class = _CppTypeName(
            f"tcspc::parameterized_data_types<{tparams}>"
        )

    def _cpp_type_name(self) -> _CppTypeName:
        return self._type_set_class
