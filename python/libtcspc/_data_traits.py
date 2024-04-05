# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
from textwrap import dedent
from typing import TypedDict, Unpack

import cppyy
from typeguard import typechecked

_cppyy_data_traits_counter = itertools.count()


class _DataTraits(TypedDict, total=False):
    abstime_type: str
    channel_type: str
    difftime_type: str
    datapoint_type: str
    bin_index_type: str
    bin_type: str


@functools.cache
@typechecked
def _data_traits_class(**kwargs: Unpack[_DataTraits]) -> str:
    types = [f"using {k} = {kwargs[k]};" for k in kwargs]  # type: ignore[literal-required]
    if not len(types):
        return "tcspc::default_data_traits"

    class_name = f"data_traits_{next(_cppyy_data_traits_counter)}"
    typedefs = ("\n" + "    " * 5).join(types)
    cppyy.cppdef(
        dedent(f"""\
            namespace tcspc::cppyy_data_traits {{
                class {class_name} : public default_data_traits {{
                    {typedefs}
                }};
            }}""")
    )
    return f"tcspc::cppyy_data_traits::{class_name}"


class DataTraits:
    def __init__(self, **kwargs: Unpack[_DataTraits]) -> None:
        self._traits_class = _data_traits_class(**kwargs)

    def cpp(self) -> str:
        return self._traits_class
