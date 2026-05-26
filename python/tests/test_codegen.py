# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._access import AccessTag
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName
from libtcspc._param import Param

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_u64_expression_literal():
    assert gencontext.u64_expression(0) == "tcspc::u64{0uLL}"
    assert gencontext.u64_expression(42) == "tcspc::u64{42uLL}"


def test_u64_expression_rejects_negative():
    with pytest.raises(ValueError):
        gencontext.u64_expression(-1)


def test_u64_expression_param():
    assert gencontext.u64_expression(Param("foo")) == "params.z_foo"


def test_u64_expression_param_special_name():
    assert gencontext.u64_expression(Param("a-b")) == "params.z_aQ2db"


def test_size_t_expression_literal():
    assert gencontext.size_t_expression(0) == "std::size_t{0uLL}"
    assert gencontext.size_t_expression(42) == "std::size_t{42uLL}"


def test_size_t_expression_rejects_negative():
    with pytest.raises(ValueError):
        gencontext.size_t_expression(-1)


def test_size_t_expression_param():
    assert gencontext.size_t_expression(Param("foo")) == "params.z_foo"


def test_string_expression_literal_quoting():
    assert gencontext.string_expression("hello") == '"hello"'
    assert gencontext.string_expression('a"b') == '"a\\"b"'
    assert gencontext.string_expression("a\\b") == '"a\\\\b"'
    assert gencontext.string_expression("a\nb") == '"a\\nb"'
    assert gencontext.string_expression("a\tb") == '"a\\tb"'
    assert gencontext.string_expression("a'b") == '"a\\\'b"'


def test_string_expression_param():
    assert gencontext.string_expression(Param("msg")) == "params.z_msg"


def test_string_expression_param_overrides_quoting():
    # The Param branch wins over string-literal quoting: a Param whose name
    # happens to contain quote characters becomes the mangled identifier, not
    # a quoted C++ string literal.
    assert gencontext.string_expression(Param('"x"')) == "params.z_Q22xQ22"


def test_bool_expression_literal():
    assert gencontext.bool_expression(True) == "true"
    assert gencontext.bool_expression(False) == "false"


def test_bool_expression_param():
    assert gencontext.bool_expression(Param("flag")) == "params.z_flag"


def test_context_varnames_used():
    ctx = _CodeGenerationContext(
        _CppIdentifier("ctx"),
        _CppIdentifier("my_params"),
        _CppIdentifier("sinks"),
    )
    assert ctx.u64_expression(Param("foo")) == "my_params.z_foo"
    assert ctx.size_t_expression(Param("foo")) == "my_params.z_foo"
    assert ctx.string_expression(Param("msg")) == "my_params.z_msg"


def test_tracker_expression_uses_context_varname():
    ctx = _CodeGenerationContext(
        _CppIdentifier("my_ctx"),
        _CppIdentifier("params"),
        _CppIdentifier("sinks"),
    )
    assert (
        ctx.tracker_expression(
            _CppTypeName("tcspc::count_access"), AccessTag("foo")
        )
        == 'my_ctx->tracker<tcspc::count_access>("foo")'
    )
