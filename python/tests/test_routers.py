# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppIdentifier,
    _identifier_from_string,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._param import Param
from libtcspc._routers import ChannelRouter, NullRouter

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_NullRouter_codegen():
    assert NullRouter()._cpp_expression(gencontext) == "tcspc::null_router()"


def test_NullRouter_has_no_parameters():
    r = NullRouter()
    assert len(r._parameters()) == 0
    assert len(r._param_encoders()) == 0


def test_ChannelRouter_literal_codegen():
    r = ChannelRouter({0: 1, 5: 0})
    assert len(r._parameters()) == 0
    assert len(r._param_encoders()) == 0
    code = r._cpp_expression(gencontext)
    assert (
        "tcspc::channel_router<2, tcspc::default_numeric_traits>(std::array{"
        in code
    )
    assert (
        "std::pair{static_cast<tcspc::default_numeric_traits::channel_type>(0)"
        ", std::size_t{1uLL}}" in code
    )
    assert (
        "std::pair{static_cast<tcspc::default_numeric_traits::channel_type>(5)"
        ", std::size_t{0uLL}}" in code
    )


def test_ChannelRouter_literal_custom_numeric_traits():
    r = ChannelRouter({0: 0}, numeric_traits=NumericTraits(channel_type="u8"))
    code = r._cpp_expression(gencontext)
    assert "tcspc::channel_router<1, nt_" in code
    assert "nt_" in code
    # The same nt_<hash> is used for both the template arg and channel_type.
    assert "::channel_type" in code


def test_ChannelRouter_param_parameters():
    p: Param = Param("ci")
    r = ChannelRouter(p, arity=2)
    params = r._parameters()
    assert len(params) == 1
    assert params[0][0] is p
    assert params[0][1] == (
        "std::array<std::pair<tcspc::default_numeric_traits::channel_type, "
        "std::size_t>, 2>"
    )


def test_ChannelRouter_param_encoder_transforms_dict_to_pairs():
    p: Param = Param("ci")
    r = ChannelRouter(p, arity=2)
    encoders = r._param_encoders()
    assert set(encoders.keys()) == {"ci"}
    assert encoders["ci"]({0: 1, 5: 0}) == [(0, 1), (5, 0)]


def test_ChannelRouter_param_codegen_references_params_struct():
    p: Param = Param("ci")
    r = ChannelRouter(p, arity=2)
    code = r._cpp_expression(gencontext)
    assert (
        f"tcspc::channel_router<2, tcspc::default_numeric_traits>("
        f"params.{_identifier_from_string('ci')})" in code
    )


def test_ChannelRouter_rejects_empty_mapping():
    with pytest.raises(ValueError):
        ChannelRouter({})


def test_ChannelRouter_rejects_negative_index():
    with pytest.raises(ValueError):
        ChannelRouter({0: -1})


def test_ChannelRouter_param_requires_arity():
    with pytest.raises(ValueError):
        ChannelRouter(Param("ci"))


def test_ChannelRouter_arity_must_match_literal_length():
    with pytest.raises(ValueError):
        ChannelRouter({0: 0, 1: 1}, arity=3)
    # Matching arity is accepted.
    ChannelRouter({0: 0, 1: 1}, arity=2)
