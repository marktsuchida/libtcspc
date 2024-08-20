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

_cpp_name_counter = itertools.count()


class _DataTypes(TypedDict, total=False):
    abstime_type: str
    channel_type: str
    difftime_type: str
    datapoint_type: str
    bin_index_type: str
    bin_type: str


@functools.cache
@typechecked
def _data_types_class(**kwargs: Unpack[_DataTypes]) -> str:
    types = [f"using {k} = {kwargs[k]};" for k in kwargs]  # type: ignore[literal-required]
    if not len(types):
        return "tcspc::default_data_types"

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
    return f"tcspc::py::data_types::{class_name}"


class DataTypes:
    def __init__(self, **kwargs: Unpack[_DataTypes]) -> None:
        self._type_set_class = _data_types_class(**kwargs)

    def cpp(self) -> str:
        return self._type_set_class
