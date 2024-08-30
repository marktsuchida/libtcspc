# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
from textwrap import dedent
from typing import TypedDict

import cppyy
from typeguard import typechecked
from typing_extensions import Unpack

from ._cpp_utils import CppTypeName

_cpp_name_counter = itertools.count()


class _DataTypes(TypedDict, total=False):
    abstime_type: CppTypeName
    channel_type: CppTypeName
    difftime_type: CppTypeName
    datapoint_type: CppTypeName
    bin_index_type: CppTypeName
    bin_type: CppTypeName


@functools.cache
@typechecked
def _data_types_class(**kwargs: Unpack[_DataTypes]) -> CppTypeName:
    types = [f"using {k} = {kwargs[k]};" for k in kwargs]  # type: ignore[literal-required]
    if not len(types):
        return CppTypeName("tcspc::default_data_types")

    class_name = f"data_types_{next(_cpp_name_counter)}"
    typedefs = ("\n" + "    " * 5).join(types)
    cppyy.cppdef(
        dedent(f"""\
            namespace tcspc::py::data_types {{
                struct {class_name} : public default_data_types {{
                    {typedefs}
                }};
            }}""")
    )
    return CppTypeName(f"tcspc::py::data_types::{class_name}")


class DataTypes:
    def __init__(self, **kwargs: Unpack[_DataTypes]) -> None:
        self._type_set_class = _data_types_class(**kwargs)

    def cpp(self) -> CppTypeName:
        return self._type_set_class
