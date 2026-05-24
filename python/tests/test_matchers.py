# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier
from libtcspc._matchers import AlwaysMatcher, ChannelMatcher, NeverMatcher
from libtcspc._param import Param

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_AlwaysMatcher():
    assert (
        AlwaysMatcher()._cpp_expression(gencontext)
        == "tcspc::always_matcher()"
    )


def test_NeverMatcher():
    assert (
        NeverMatcher()._cpp_expression(gencontext) == "tcspc::never_matcher()"
    )


def test_ChannelMatcher():
    code = ChannelMatcher(3)._cpp_expression(gencontext)
    assert "tcspc::channel_matcher<tcspc::default_numeric_traits>(" in code
    assert "tcspc::arg::channel<" in code
    assert "(3)" in code


def test_ChannelMatcher_param():
    m = ChannelMatcher(Param("ch"))
    params = m._parameters()
    assert len(params) == 1
    assert params[0][0] == Param("ch")
    assert "params.z_ch" in m._cpp_expression(gencontext)
