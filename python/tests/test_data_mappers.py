# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier
from libtcspc._data_mappers import (
    ChannelDataMapper,
    CountDataMapper,
    DifftimeDataMapper,
)

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DifftimeDataMapper():
    assert (
        DifftimeDataMapper()._cpp_expression(gencontext)
        == "tcspc::difftime_data_mapper<tcspc::default_numeric_traits>()"
    )


def test_CountDataMapper():
    assert (
        CountDataMapper()._cpp_expression(gencontext)
        == "tcspc::count_data_mapper<tcspc::default_numeric_traits>()"
    )


def test_ChannelDataMapper():
    assert (
        ChannelDataMapper()._cpp_expression(gencontext)
        == "tcspc::channel_data_mapper<tcspc::default_numeric_traits>()"
    )
