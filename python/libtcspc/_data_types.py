# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import TypedDict

from typing_extensions import Unpack

from ._cpp_utils import CppTypeName


class _DataTypes(TypedDict, total=False):
    abstime_type: CppTypeName
    channel_type: CppTypeName
    difftime_type: CppTypeName
    count_type: CppTypeName
    datapoint_type: CppTypeName
    bin_index_type: CppTypeName
    bin_type: CppTypeName


class DataTypes:
    def __init__(self, **kwargs: Unpack[_DataTypes]) -> None:
        def typ(category):
            return kwargs.get(
                f"{category}_type",
                CppTypeName(f"tcspc::default_data_types::{category}_type"),
            )

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
